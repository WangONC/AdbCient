#include "AdbClient/file_sync_protocol.h"
#include <memory>
#include <deque>
#include <string>
#include <vector>
#include "AdbClient/AdbUtils.h"
#include "AdbClient/client.h"
#include "AdbClient/adb_io.h"
#include "misc/StringUtils.h"
#include "AdbClient/file_sync_client.h"
#include <stdexcept>
#include "misc/SystemUtils.h"
#include "AdbClient/compression_utils.h"
#include <variant>

#ifdef _WIN32
typedef __int64 ssize_t;
#else
#include <sys/types.h>
#endif


static void ensure_trailing_separators(std::string& local_path, std::string& remote_path) {
	if (!(local_path.back() == '\\' || local_path.back() == '/')) {
		local_path.push_back('\\');

	}
	if (remote_path.back() != '/') {
		remote_path.push_back('/');

	}

}

struct copyinfo {
	std::string lpath;
	std::string rpath;
	int64_t time = 0;
	uint32_t mode;
	uint64_t size = 0;
	bool skip = false;

	copyinfo(const std::string& local_path,
		const std::string& remote_path,
		const std::string& name,
		unsigned int mode)
		: lpath(local_path), rpath(remote_path), mode(mode) {
		ensure_trailing_separators(lpath, rpath);
		lpath.append(name);
		rpath.append(name);
		if (S_ISDIR(mode)) {
			ensure_trailing_separators(lpath, rpath);
		}
	}
};

struct syncsendbuf {
	unsigned id;
	unsigned size;
	char data[SYNC_DATA_MAX];

};

class SyncConnection {
public:
	std::string error; // 错误原因

	SyncConnection::SyncConnection(std::string serial, uint64_t* transport_id) {
		acknowledgement_buffer_.resize(sizeof(sync_status) + SYNC_DATA_MAX); // 根据源码中解释，不使用std::make_unique是因为这不会进行值初始化，避免了不必要的性能开销
		std::string error;
		features = adb_get_feature(serial, transport_id, &error);
		if (features.empty()) {
			throw std::runtime_error("failed to get feature set: " + error);
		}
		else {
			have_stat_v2_ = CanUseFeature(features, kFeatureStat2);
			have_ls_v2_ = CanUseFeature(features, kFeatureLs2);
			have_sendrecv_v2_ = CanUseFeature(features, kFeatureSendRecv2);
			have_sendrecv_v2_brotli_ = CanUseFeature(features, kFeatureSendRecv2Brotli);
			have_sendrecv_v2_lz4_ = CanUseFeature(features, kFeatureSendRecv2LZ4);
			have_sendrecv_v2_zstd_ = CanUseFeature(features, kFeatureSendRecv2Zstd);
			have_sendrecv_v2_dry_run_send_ = CanUseFeature(features, kFeatureSendRecv2DryRunSend);
			std::string error_;
			fd = BaseClient::adb_connect("sync:", serial, transport_id, &error_); // 切换到同步模式，用于同步文件 https://android.googlesource.com/platform/system/adb/+/refs/heads/main/SYNC.TXT
			if (fd < 0) {
				throw std::runtime_error("connect failed: " + error_);
			}
		}
		m_serial = serial;
		m_transport_id = transport_id;
	}

	SyncConnection::~SyncConnection() {
		if (!IsValid()) return;
		if (SendQuit()) {
			ReadOrderlyShutdown(fd, SD_BOTH);
		}

	}

	bool IsValid() { return fd >= 0; }
	bool HaveSendRecv2() const { return have_sendrecv_v2_; }
	bool HaveSendRecv2Brotli() const { return have_sendrecv_v2_brotli_; }
	bool HaveSendRecv2LZ4() const { return have_sendrecv_v2_lz4_; }
	bool HaveSendRecv2Zstd() const { return have_sendrecv_v2_zstd_; }
	bool HaveSendRecv2DryRunSend() const { return have_sendrecv_v2_dry_run_send_; }

	CompressionType ResolveCompressionType(CompressionType compression) const {
		if (compression == CompressionType::Any) {
			if (HaveSendRecv2Zstd()) {
				return CompressionType::Zstd;
			}
			else if (HaveSendRecv2LZ4()) {
				return CompressionType::LZ4;
			}
			else if (HaveSendRecv2Brotli()) {
				return CompressionType::Brotli;
			}
			return CompressionType::None;
		}
		return compression;
	}

	bool SendRequest(int id, const std::string& path) {
		if (path.length() > 1024) {
			error = StringPrintf("SendRequest failed: path too long: %zu", path.length());
			errno = ENAMETOOLONG;
			return false;
		}

		// Sending header and payload in a single write makes a noticeable
		// difference to "adb sync" performance.
		std::vector<char> buf(sizeof(SyncRequest) + path.length());
		SyncRequest* req = reinterpret_cast<SyncRequest*>(&buf[0]);
		req->id = id;
		req->path_length = path.length();
		char* data = reinterpret_cast<char*>(req + 1);
		memcpy(data, path.data(), path.length());
		return WriteFdExactly(fd, buf.data(), buf.size());
	}

	bool SendQuit() {
		return SendRequest(ID_QUIT, "");

	}

	bool SendStat(const std::string& path) {
		if (!have_stat_v2_) {
			errno = ENOTSUP;
			return false;
		}
		return SendRequest(ID_STAT_V2, path);
	}

	bool SendLstat(const std::string& path) {
		if (have_stat_v2_) {
			return SendRequest(ID_LSTAT_V2, path);
		}
		else {
			return SendRequest(ID_LSTAT_V1, path);
		}
	}

	bool FinishStat(struct stat* st) {
		syncmsg msg;
		// 改成直接使用原始的syncmsg结构
		memset(st, 0, sizeof(*st));
		if (have_stat_v2_) {
			if (!ReadFdExactly(fd, &msg.stat_v2, sizeof(msg.stat_v2))) {
				error = "protocol fault: failed to read stat response";
			}

			if (msg.stat_v2.id != ID_LSTAT_V2 && msg.stat_v2.id != ID_STAT_V2) {
				error = "protocol fault: stat response has wrong message id: " + std::to_string(msg.stat_v2.id);
			}

			if (msg.stat_v2.error != 0) {
				return false;
			}

			
			st->st_dev = msg.stat_v2.dev;
			st->st_ino = msg.stat_v2.ino;
			st->st_mode = msg.stat_v2.mode;
			st->st_nlink = msg.stat_v2.nlink;
			st->st_uid = msg.stat_v2.uid;
			st->st_gid = msg.stat_v2.gid;
			st->st_size = msg.stat_v2.size;
			st->st_atime = msg.stat_v2.atime;
			st->st_mtime = msg.stat_v2.mtime;
			st->st_ctime = msg.stat_v2.ctime;
			return true;
		}
		else {
			if (!ReadFdExactly(fd, &msg.stat_v1, sizeof(msg.stat_v1))) {
				error = "protocol fault: failed to read stat response";
			}

			if (msg.stat_v1.id != ID_LSTAT_V1) {
				error = "protocol fault: stat response has wrong message id: " + std::to_string(msg.stat_v1.id);
			}

			if (msg.stat_v1.mode == 0 && msg.stat_v1.size == 0 && msg.stat_v1.mtime == 0) {
				// There's no way for us to know what the error was.
				errno = ENOPROTOOPT;
				return false;
			}

			st->st_mode = msg.stat_v1.mode;
			st->st_size = static_cast<uint64_t>(msg.stat_v1.size);
			st->st_ctime = static_cast<int64_t>(msg.stat_v1.mtime);
			st->st_mtime = static_cast<int64_t>(msg.stat_v1.mtime);
		}

		return true;
	}

	std::vector<std::string> Features() {
		return features;
	}

	void RecordFileSent(std::string from, std::string to) {
		//RecordFilesTransferred(1);
		deferred_acknowledgements_.emplace_back(std::move(from), std::move(to));
	}

	bool SendSend2(std::string_view path, unsigned short mode, CompressionType compression, bool dry_run) {
		if (path.length() > 1024) {
			error = StringPrintf("SendRequest failed: path too long: %zu", path.length());
			errno = ENAMETOOLONG;
			return false;
		}

		std::vector<char> buf;

		SyncRequest req;
		req.id = ID_SEND_V2;
		req.path_length = path.length();

		syncmsg msg;
		msg.send_v2_setup.id = ID_SEND_V2;
		msg.send_v2_setup.mode = mode;
		msg.send_v2_setup.flags = 0;
		switch (compression) {
		case CompressionType::None:
			break;

		case CompressionType::Brotli:
			msg.send_v2_setup.flags = kSyncFlagBrotli;
			break;

		case CompressionType::LZ4:
			msg.send_v2_setup.flags = kSyncFlagLZ4;
			break;

		case CompressionType::Zstd:
			msg.send_v2_setup.flags = kSyncFlagZstd;
			break;

		case CompressionType::Any:
			error = "unexpected CompressionType::Any";
		}

		if (dry_run) {
			msg.send_v2_setup.flags |= kSyncFlagDryRun;
		}

		buf.resize(sizeof(SyncRequest) + path.length() + sizeof(msg.send_v2_setup));

		void* p = buf.data();

		p = mempcpy(p, &req, sizeof(SyncRequest));
		p = mempcpy(p, path.data(), path.length());
		p = mempcpy(p, &msg.send_v2_setup, sizeof(msg.send_v2_setup));

		
		return WriteFdExactly(fd, buf.data(), buf.size());
	}

	// Sending header, payload, and footer in a single write makes a huge
	// difference to "adb sync" performance.
	bool SendSmallFile(const std::string& path, unsigned short mode, const std::string& lpath,
		const std::string& rpath, unsigned mtime, const char* data,
		size_t data_length, bool dry_run) {
		if (dry_run) {
			// We need to use send v2 for dry run.
			return SendLargeFile(path, mode, lpath, rpath, mtime, CompressionType::None, dry_run);
		}

		std::string path_and_mode = StringPrintf("%s,%d", path.c_str(), mode);
		if (path_and_mode.length() > 1024) {
			error = StringPrintf("SendSmallFile failed: path too long: %zu", path_and_mode.length());
			errno = ENAMETOOLONG;
			return false;
		}

		std::vector<char> buf(sizeof(SyncRequest) + path_and_mode.length() + sizeof(SyncRequest) +
			data_length + sizeof(SyncRequest));
		char* p = &buf[0];

		SyncRequest* req_send = reinterpret_cast<SyncRequest*>(p);
		req_send->id = ID_SEND_V1;
		req_send->path_length = path_and_mode.length();
		p += sizeof(SyncRequest);
		memcpy(p, path_and_mode.data(), path_and_mode.size());
		p += path_and_mode.length();

		SyncRequest* req_data = reinterpret_cast<SyncRequest*>(p);
		req_data->id = ID_DATA;
		req_data->path_length = data_length;
		p += sizeof(SyncRequest);
		memcpy(p, data, data_length);
		p += data_length;

		SyncRequest* req_done = reinterpret_cast<SyncRequest*>(p);
		req_done->id = ID_DONE;
		req_done->path_length = mtime;
		p += sizeof(SyncRequest);

		WriteOrDie(lpath, rpath, &buf[0], (p - &buf[0]));

		RecordFileSent(lpath, rpath);
		//RecordBytesTransferred(data_length);
		//ReportProgress(rpath, data_length, data_length);
		return true;
	}

	bool SendLargeFile(const std::string& path, unsigned short mode, const std::string& lpath,
		const std::string& rpath, unsigned mtime, CompressionType compression,
		bool dry_run) {
		if (dry_run && !HaveSendRecv2DryRunSend()) {
			error = StringPrintf("dry-run not supported by the device");
			return false;
		}

		if (!HaveSendRecv2()) {
			return SendLargeFileLegacy(path, mode, lpath, rpath, mtime);
		}

		compression = ResolveCompressionType(compression);

		if (!SendSend2(path, mode, compression, dry_run)) {
			error = StringPrintf("failed to send ID_SEND_V2 message '%s': %s", path.c_str(), strerror(errno));
			return false;
		}

		struct stat st;
		if (stat(lpath.c_str(), &st) == -1) {
			error = StringPrintf("cannot stat '%s': %s", lpath.c_str(), strerror(errno));
			return false;
		}

		uint64_t total_size = st.st_size;
		uint64_t bytes_copied = 0;

		HANDLE lfd(open(lpath.c_str(), GENERIC_READ, &error));
		if (lfd == INVALID_HANDLE_VALUE) {
			error = StringPrintf("opening '%s' locally failed: %s", lpath.c_str(), strerror(errno));
			return false;
		}

		syncsendbuf sbuf;
		sbuf.id = ID_DATA;

		std::variant<std::monostate, NullEncoder, BrotliEncoder, LZ4Encoder, ZstdEncoder>
			encoder_storage;
		Encoder* encoder = nullptr;
		switch (compression) {
		case CompressionType::None:
			encoder = &encoder_storage.emplace<NullEncoder>(SYNC_DATA_MAX);
			break;

		case CompressionType::Brotli:
			encoder = &encoder_storage.emplace<BrotliEncoder>(SYNC_DATA_MAX);
			break;

		case CompressionType::LZ4:
			encoder = &encoder_storage.emplace<LZ4Encoder>(SYNC_DATA_MAX);
			break;

		case CompressionType::Zstd:
			encoder = &encoder_storage.emplace<ZstdEncoder>(SYNC_DATA_MAX);
			break;

		case CompressionType::Any:
			error = "unexpected CompressionType::Any";
		}

		bool sending = true;
		while (sending) {
			Block input(SYNC_DATA_MAX);
			input.resize(SYNC_DATA_MAX);
			int r = read(lfd, input.data(), input.size(),&error);
			if (r < 0) {
				error = StringPrintf("reading '%s' locally failed: %s", lpath.c_str(), strerror(errno));
				return false;
			}

			if (r == 0) {
				encoder->Finish();
			}
			else {
				input.resize(r);
				encoder->Append(std::move(input));
				//RecordBytesTransferred(r);
				bytes_copied += r;
				//ReportProgress(rpath, bytes_copied, total_size);
			}

			while (true) {
				Block output;
				EncodeResult result = encoder->Encode(&output);
				if (result == EncodeResult::Error) {
					error = StringPrintf("compressing '%s' locally failed", lpath.c_str());
					return false;
				}

				if (!output.empty()) {
					sbuf.size = output.size();
					memcpy(sbuf.data, output.data(), output.size());
					WriteOrDie(lpath, rpath, &sbuf, sizeof(SyncRequest) + output.size());
				}

				if (result == EncodeResult::Done) {
					sending = false;
					break;
				}
				else if (result == EncodeResult::NeedInput) {
					break;
				}
				else if (result == EncodeResult::MoreOutput) {
					continue;
				}
			}
		}

		syncmsg msg;
		msg.data.id = ID_DONE;
		msg.data.size = mtime;
		RecordFileSent(lpath, rpath);
		return WriteOrDie(lpath, rpath, &msg.data, sizeof(msg.data));
	}

	bool SendLargeFileLegacy(const std::string& path, unsigned short mode, const std::string& lpath,
		const std::string& rpath, unsigned mtime) {
		std::string path_and_mode = StringPrintf("%s,%d", path.c_str(), mode);
		if (!SendRequest(ID_SEND_V1, path_and_mode)) {
			error = StringPrintf("failed to send ID_SEND_V1 message '%s': %s", path_and_mode.c_str(),
				strerror(errno));
			return false;
		}

		struct stat st;
		if (stat(lpath.c_str(), &st) == -1) {
			error = StringPrintf("cannot stat '%s': %s", lpath.c_str(), strerror(errno));
			return false;
		}

		uint64_t total_size = st.st_size;
		uint64_t bytes_copied = 0;

		HANDLE lfd(open(lpath.c_str(), GENERIC_READ, &error));
		if (lfd == INVALID_HANDLE_VALUE) {
			error = StringPrintf("opening '%s' locally failed: %s", lpath.c_str(), strerror(errno));
			return false;
		}

		syncsendbuf sbuf;
		sbuf.id = ID_DATA;

		while (true) {
			int bytes_read = read(lfd, sbuf.data, max, &error);
			if (bytes_read == -1) {
				error = StringPrintf("reading '%s' locally failed: %s", lpath.c_str(), strerror(errno));
				return false;
			}
			else if (bytes_read == 0) {
				break;
			}

			sbuf.size = bytes_read;
			WriteOrDie(lpath, rpath, &sbuf, sizeof(SyncRequest) + bytes_read);

			//RecordBytesTransferred(bytes_read);
			bytes_copied += bytes_read;
			//ReportProgress(rpath, bytes_copied, total_size);
		}

		syncmsg msg;
		msg.data.id = ID_DONE;
		msg.data.size = mtime;
		RecordFileSent(lpath, rpath);
		return WriteOrDie(lpath, rpath, &msg.data, sizeof(msg.data));
	}

	bool ReportCopyFailure(const std::string& from, const std::string& to, const syncmsg& msg) {
		Block buf(msg.status.msglen + 1);
		if (!ReadFdExactly(fd, &buf[0], msg.status.msglen)) {
			error = StringPrintf("failed to copy '%s' to '%s'; failed to read reason (!): %s", from.c_str(),
				to.c_str(), strerror(errno));
			return false;
		}
		buf[msg.status.msglen] = 0;
		error = StringPrintf("failed to copy '%s' to '%s': remote %s", from.c_str(), to.c_str(), &buf[0]);
		return false;
	}

	bool ReadAcknowledgements(bool read_all = false) {
		// We need to read enough such that adbd's intermediate socket's write buffer can't be
		// full. The default buffer on Linux is 212992 bytes, but there's 576 bytes of bookkeeping
		// overhead per write. The worst case scenario is a continuous string of failures, since
		// each logical packet is divided into two writes. If our packet size if conservatively 512
		// bytes long, this leaves us with space for 128 responses.
		constexpr size_t max_deferred_acks = 128;
		auto& buf = acknowledgement_buffer_;
		adb_pollfd pfd = { .fd = fd, .events = POLLIN };
		while (!deferred_acknowledgements_.empty()) {
			bool should_block = read_all || deferred_acknowledgements_.size() >= max_deferred_acks;

			ssize_t rc = adb_poll(&pfd, 1, should_block ? -1 : 0);
			if (rc == 0) {
				//CHECK(!should_block);
				return true;
			}

			if (acknowledgement_buffer_.size() < sizeof(sync_status)) {
				const ssize_t header_bytes_left = sizeof(sync_status) - buf.size();
				ssize_t rc = recv(fd, buf.end(), header_bytes_left, 0);
				if (rc <= 0) {
					error = "failed to read copy response";
					return false;
				}

				buf.resize(buf.size() + rc);
				if (rc != header_bytes_left) {
					// Early exit if we run out of data in the socket.
					return true;
				}

				if (!should_block) {
					// We don't want to read again yet, because the socket might be empty.
					continue;
				}
			}

			auto* hdr = reinterpret_cast<sync_status*>(buf.data());
			if (hdr->id == ID_OKAY) {
				buf.resize(0);
				if (hdr->msglen != 0) {
					error = StringPrintf("received ID_OKAY with msg_len (%I32u != 0", hdr->msglen);
					return false;
				}
				CopyDone();
				continue;
			}
			else if (hdr->id != ID_FAIL) {
				error = StringPrintf("unexpected response from daemon: id = %#I32x", hdr->id);
				return false;
			}
			else if (hdr->msglen > SYNC_DATA_MAX) {
				error = StringPrintf("too-long message length from daemon: msglen = %I32u", hdr->msglen);
				return false;
			}

			const ssize_t msg_bytes_left = hdr->msglen + sizeof(sync_status) - buf.size();
			//CHECK_GE(msg_bytes_left, 0);
			if (msg_bytes_left > 0) {
				ssize_t rc = recv(fd, buf.end(), msg_bytes_left, 0);
				if (rc <= 0) {
					error = "failed to read copy failure message";
					return false;
				}

				buf.resize(buf.size() + rc);
				if (rc != msg_bytes_left) {
					if (should_block) {
						continue;
					}
					else {
						return true;
					}
				}

				std::string msg(buf.begin() + sizeof(sync_status), buf.end());
				ReportDeferredCopyFailure(msg);
				buf.resize(0);
				return false;
			}
		}

		return true;
	}

	void ReportDeferredCopyFailure(const std::string& msg) {
		auto& [from, to] = deferred_acknowledgements_.front();
		error = StringPrintf("failed to copy '%s' to '%s': remote %s", from.c_str(), to.c_str(), msg.c_str());
		deferred_acknowledgements_.pop_front();
	}

	void CopyDone() { deferred_acknowledgements_.pop_front(); }

	SOCKET fd;
	size_t max;

private:
	std::deque<std::pair<std::string, std::string>> deferred_acknowledgements_;
	Block acknowledgement_buffer_; // 源码中是Block，定义在adb/type.h中，是一个封装后的std::unique_ptr<char[]> data;缓冲区,本质上是std::vector<char>
	std::vector<std::string> features;
	bool have_stat_v2_;
	bool have_ls_v2_;
	bool have_sendrecv_v2_;
	bool have_sendrecv_v2_brotli_;
	bool have_sendrecv_v2_lz4_;
	bool have_sendrecv_v2_zstd_;
	bool have_sendrecv_v2_dry_run_send_;

	
	std::string m_serial;
	uint64_t* m_transport_id;

	bool WriteOrDie(const std::string& from, const std::string& to, const void* data,
		size_t data_length) {
		if (!WriteFdExactly(fd, data, data_length)) {
			if (errno == ECONNRESET) {
				// Assume adbd told us why it was closing the connection, and
				// try to read failure reason from adbd.
				syncmsg msg;
				if (!ReadFdExactly(fd, &msg.status, sizeof(msg.status))) {
					error = StringPrintf("failed to copy '%s' to '%s': no response: %s", from.c_str(), to.c_str(),
						strerror(errno));
				}
				else if (msg.status.id != ID_FAIL) {
					error = StringPrintf("failed to copy '%s' to '%s': not ID_FAIL: %d", from.c_str(), to.c_str(),
						msg.status.id);
				}
				else {
					ReportCopyFailure(from, to, msg);
				}
			}
			else {
				error = StringPrintf("%zu-byte write failed: %s", data_length, strerror(errno));
			}
			return false;
		}
		return true;
	}
};

// dirname("//foo") returns "//", so we can't do the obvious `path == "/"`.
static bool is_root_dir(std::string path) {
	for (char c : path) {
		if (c != '/') {
			return false;
		}
	}
	return true;
}

static bool IsDotOrDotDot(const char* name) {
	return name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));

}



static bool local_build_list(SyncConnection& sc, std::vector<copyinfo>* file_list,
	std::vector<std::string>* directory_list, const std::string& lpath,
	const std::string& rpath) {
	std::vector<copyinfo> dirlist;
	std::wstring *werror;
	std::vector<DirectoryEntry> des = readDirectory(CharToWstring(lpath.c_str()), werror);
	//std::unique_ptr<DIR, int (*)(DIR*)> dir(_wopendir(lpath.c_str()), closedir); // 这里实际上是维护了一个dir对象，首先打开第一个，然后通过readdir遍历目录，获取文件信息
	//if (!dir) {
	//	sc.error = StringPrintf("cannot open '%s': %s", lpath.c_str(), strerror(errno));
	//	return false;
	//}

	sc.error = WstringToString(werror->c_str()); // 或许会产生编码问题

	//dirent* de; // 这里的dirent结构基本上就是目录内容，名称、名称长度，类型（普通文件，目录，字符设备）
	for(DirectoryEntry de : des){
	//while ((de = readdir(dir.get()))) {
		if (IsDotOrDotDot(WstringToString(de.d_name).c_str())) {
			continue;
		}

		std::string stat_path = lpath + WstringToString(de.d_name);

		struct stat st;
		if (lstat(stat_path.c_str(), &st) == -1) {
			sc.error = StringPrintf("cannot lstat '%s': %s", stat_path.c_str(),
				strerror(errno));
			continue;
		}

		copyinfo ci(lpath, rpath, WstringToString(de.d_name), st.st_mode);
		if (S_ISDIR(st.st_mode)) {
			dirlist.push_back(ci);
		}
		else {
			
			if (!(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))) { // 只有一般文件和软连接（windows中没有）需要放置，为啥连软连接也要放？？
				sc.error = StringPrintf("skipping special file '%s' (mode = 0o%o)", lpath.c_str(), st.st_mode);
				ci.skip = true;
			}
			ci.time = st.st_mtime;
			ci.size = st.st_size;
			file_list->push_back(ci); // 添加进需要放置的文件列表中
		}
	}

	// Close this directory and recurse.
	//dir.reset();

	for (const copyinfo& ci : dirlist) {
		directory_list->push_back(ci.rpath);
		local_build_list(sc, file_list, directory_list, ci.lpath, ci.rpath);
	}

	return true;
}

static bool sync_stat(SyncConnection& sc, const std::string& path, struct stat* st) {
	return sc.SendStat(path) && sc.FinishStat(st);
}

static bool sync_lstat(SyncConnection& sc, const std::string& path, struct stat* st) {
	return sc.SendLstat(path) && sc.FinishStat(st);
}

static bool sync_stat_fallback(SyncConnection& sc, const std::string& path, struct stat* st) {
	if (sync_stat(sc, path, st)) {
		return true;
	}

	if (errno != ENOTSUP) {
		return false;
	}

	// Try to emulate the parts we can when talking to older adbds.
	bool lstat_result = sync_lstat(sc, path, st);
	if (!lstat_result) {
		return false;
	}

	if (S_ISLNK(st->st_mode)) {
		// If the target is a symlink, figure out whether it's a file or a directory.
		// Also, zero out the st_size field, since no one actually cares what the path length is.
		st->st_size = 0;
		std::string dir_path = path;
		dir_path.push_back('/');
		struct stat tmp_st;

		st->st_mode &= ~S_IFMT;
		if (sync_lstat(sc, dir_path, &tmp_st)) {
			st->st_mode |= S_IFDIR;
		}
		else {
			st->st_mode |= S_IFREG;
		}
	}
	return true;
}

// 发送文件
static bool sync_send(SyncConnection& sc, const std::string& lpath, const std::string& rpath, unsigned mtime, unsigned short mode, bool sync, CompressionType compression,
	bool dry_run) {
	if (sync) {
		struct stat st;
		if (sync_lstat(sc, rpath, &st)) {
			if (st.st_mtime == static_cast<time_t>(mtime)) {
				//sc.RecordFilesSkipped(1);
				return true;
			}
		}
	}

	if (S_ISLNK(mode)) {
#if !defined(_WIN32)
		char buf[PATH_MAX];
		ssize_t data_length = readlink(lpath.c_str(), buf, PATH_MAX - 1);
		if (data_length == -1) {
			sc.Error("readlink '%s' failed: %s", lpath.c_str(), strerror(errno));
			return false;
		}
		buf[data_length++] = '\0';

		if (!sc.SendSmallFile(rpath, mode, lpath, rpath, mtime, buf, data_length, dry_run)) {
			return false;
		}
		return sc.ReadAcknowledgements(sync);
#endif
	}

	struct stat st;
	if (stat(lpath.c_str(), &st) == -1) {
		sc.error = StringPrintf("failed to stat local file '%s': %s", lpath.c_str(), strerror(errno));
		return false;
	}
	if (st.st_size < SYNC_DATA_MAX) {
		std::string data;
		if (!ReadFileToString(lpath, &data, true, &sc.error)) {
			sc.error = StringPrintf("failed to read all of '%s': %s", lpath.c_str(), strerror(errno));
			return false;
		}
		if (!sc.SendSmallFile(rpath, mode, lpath, rpath, mtime, data.data(), data.size(),
			dry_run)) {
			return false;
		}
	}
	else {
		if (!sc.SendLargeFile(rpath, mode, lpath, rpath, mtime, compression, dry_run)) {
			return false;
		}
	}
	return sc.ReadAcknowledgements(sync);
}

// 拷贝目标路径
static bool copy_local_dir_remote(std::string m_serial, uint64_t* m_transport_id, SyncConnection& sc, std::string lpath, std::string rpath,
	bool check_timestamps, bool list_only,
	CompressionType compression, bool dry_run) {
	//sc.NewTransfer();

	// Make sure that both directory paths end in a slash.
	// Both paths are known to be nonempty, so we don't need to check.
	ensure_trailing_separators(lpath, rpath); // 目录格式化

	// Recursively build the list of files to copy.
	std::vector<copyinfo> file_list;
	std::vector<std::string> directory_list;

	for (std::string path = rpath; !is_root_dir(path); path = Dirname(path)) { // 遍历获取目标设备路径，一直到根目录（用于递归创建目录）
		directory_list.push_back(path);
	}
	std::reverse(directory_list.begin(), directory_list.end()); // 反向

	int skipped = 0;
	if (!local_build_list(sc, &file_list, &directory_list, lpath, rpath)) {// 获取本地目录和文件列表
		return false;
	}

	// b/110953234:
	// P shipped with a bug that causes directory creation as a side-effect of a push to fail.
	// Work around this by explicitly doing a mkdir via shell.
	//
	// Devices that don't support shell_v2 are unhappy if we try to send a too-long packet to them,
	// but they're not affected by this bug, so only apply the workaround if we have shell_v2.
	//
	// TODO(b/25457350): We don't preserve permissions on directories.
	// TODO: Find all of the leaves and `mkdir -p` them instead?
	std::string* r, * e;
	if (!CanUseFeature(sc.Features(), std::string(kFeatureFixedPushMkdir)) &&
		CanUseFeature(sc.Features(), std::string(kFeatureShell2))) { // 咋的就因为不支持shell_v2就不给push文件了？
		std::string cmd = "mkdir";
		for (const auto& dir : directory_list) {
			std::string escaped_path = escape_arg(dir); // 转换适用于目录的字符串
			if (escaped_path.size() > 16384) {
				// Somewhat arbitrarily limit that probably won't ever happen.
				sc.error = StringPrintf("path too long: %s", escaped_path.c_str());
				return false;
			}

			// The maximum should be 64kiB, but that's not including other stuff that gets tacked
			// onto the command line, so let's be a bit conservative.
			if (cmd.size() + escaped_path.size() > 32768) { // 单条命令执行，创建文件夹
				// Dispatch the command, ignoring failure (since the directory might already exist).
				BaseClient::send_shell_command(m_serial, m_transport_id, cmd, false, r, e);
				cmd = "mkdir";
			}
			cmd += " ";
			cmd += escaped_path;
		}

		if (cmd != "mkdir") {
			BaseClient::send_shell_command(m_serial, m_transport_id, cmd, false, r, e);
		}
	}

	if (check_timestamps) {
		for (const copyinfo& ci : file_list) {
			if (!sc.SendLstat(ci.rpath)) {
				sc.error = StringPrintf("failed to send lstat");
				return false;
			}
		}
		for (copyinfo& ci : file_list) {
			struct stat st;
			if (sc.FinishStat(&st)) {
				if (st.st_size == static_cast<__int64>(ci.size) && st.st_mtime == ci.time) {
					ci.skip = true;
				}
			}
		}
	}

	//sc.ComputeExpectedTotalBytes(file_list); // 用于处理传输进度

	for (const copyinfo& ci : file_list) {
		if (!ci.skip) {
			if (list_only) {
				//sc.Println("would push: %s -> %s", ci.lpath.c_str(), ci.rpath.c_str());
			}
			else {
				if (!sync_send(sc, ci.lpath, ci.rpath, ci.time, ci.mode, false, compression,
					dry_run)) {
					return false;
				}
			}
		}
		else {
			skipped++;
		}
	}

	//sc.RecordFilesSkipped(skipped);
	bool success = sc.ReadAcknowledgements(true);
	//sc.ReportTransferRate(lpath, TransferDirection::push);
	return success;
}

bool do_sync_push(std::string m_serial, uint64_t* m_transport_id, const std::vector<const char*>& srcs, const char* dst, bool sync, CompressionType compression, bool dry_run, std::string *error)
{
	// 创建文件同步对象
	SyncConnection sc(m_serial, m_transport_id);
	if (!sc.IsValid()) return false;

	bool success = true;
	bool dst_exists;
	bool dst_isdir;

	struct adb_stat st;
	// 需要使用stat检查目标路径在目标设备中的状态
	if (sync_stat_fallback(sc, dst, &st)) {
		dst_exists = true;
		dst_isdir = S_ISDIR(st.st_mode); // 检查是不是文件夹
	}
	else
	{
		if (errno == ENOENT || errno == ENOPROTOOPT)
		{
			dst_exists = false;
			dst_isdir = false;
		}
	}

	// 如果目的地址不是目录
	if (!dst_isdir) {
		if (srcs.size() > 1) { // 那就只能有一个输入文件
			*error = StringPrintf("target '%s' is not a directory", dst);
			return false;
		}
		else {
			size_t dst_len = strlen(dst);

			// A path that ends with a slash doesn't have to be a directory if
			// it doesn't exist yet.
			if (dst[dst_len - 1] == '/' && dst_exists) {
				*error = StringPrintf("failed to access '%s': Not a directory", dst);
				return false;
			}
		}
	}

	for (const char* src_path : srcs) { // 遍历输入目录
		const char* dst_path = dst;
		struct stat st;
		if (stat(src_path, &st) == -1) { // 检查输入目录属性
			*error = StringPrintf("cannot stat '%s': %s", src_path, strerror(errno));
			success = false;
			continue;
		}

		if (S_ISDIR(st.st_mode)) { // 当前输入路径是目录
			std::string dst_dir = dst;

			// If the destination path existed originally, the source directory
			// should be copied as a child of the destination.
			if (dst_exists) {
				if (!dst_isdir) {
					*error = StringPrintf("target '%s' is not a directory", dst);
					return false;
				}
				// dst is a POSIX path, so we don't want to use the sysdeps
				// helpers here.
				if (dst_dir.back() != '/') {
					dst_dir.push_back('/');
				}
				dst_dir.append(Basename(src_path)); // 添加为目标路径的子目录 
			}

			success &=
				copy_local_dir_remote(m_serial,m_transport_id,sc, src_path, dst_dir, sync, false, compression, dry_run);
			continue;
		}
		else if (!(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))) {
			//sc.Warning("skipping special file '%s' (mode = 0o%o)", src_path, st.st_mode);
			continue;
		}

		std::string path_holder;
		if (dst_isdir) {
			// If we're copying a local file to a remote directory,
			// we really want to copy to remote_dir + "/" + local_filename.
			path_holder = dst_path;
			if (path_holder.back() != '/') {
				path_holder.push_back('/');
			}
			path_holder += Basename(src_path);
			dst_path = path_holder.c_str();
		}

		success &= sync_send(sc, src_path, dst_path, st.st_mtime, st.st_mode, sync, compression,
			dry_run);
	}

	success &= sc.ReadAcknowledgements(true);
	return success;
}
