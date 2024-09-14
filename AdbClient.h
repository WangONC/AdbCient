#pragma once

#include "CommandExecution.h"
#include <optional>

static const size_t MAX_PAYLOAD = 1024 * 1024;

// http://aospxref.com/android-14.0.0_r2/xref/packages/modules/adb/adb.h?fi=kCsOffline#kCsOffline
enum ConnectionState {
	kCsAny = -1,

	kCsConnecting = 0,  // Haven't received a response from the device yet.
	kCsAuthorizing,     // Authorizing with keys from ADB_VENDOR_KEYS.
	kCsUnauthorized,    // ADB_VENDOR_KEYS exhausted, fell back to user prompt.
	kCsNoPerm,          // Insufficient permissions to communicate with the device.
	kCsDetached,        // USB device that's detached from the adb server.
	kCsOffline,

	// After CNXN packet, the ConnectionState describes not a state but the type of service
	// on the other end of the transport.
	kCsBootloader,  // Device running fastboot OS (fastboot) or userspace fastboot (fastbootd).
	kCsDevice,      // Device running Android OS (adbd).
	kCsHost,        // What a device sees from its end of a Transport (adb host).
	kCsRecovery,    // Device with bootloader loaded but no ROM OS loaded (adbd).
	kCsSideload,    // Device running Android OS Sideload mode (minadbd sideload mode).
	kCsRescue,      // Device running Android OS Rescue mode (minadbd rescue mode).

	kCsUnknown,		// 兼容未知
};

typedef struct {
	std::string serial;
	ConnectionState deviceState;
	std::string devpath;
	std::string product;
	std::string model;
	std::string device;
	std::string transport_id;
} DeviceInfo;

class AdbClient
{


public:
	AdbClient();
	AdbClient(std::string serial);
	AdbClient(uint64_t* transport_id);
	AdbClient(std::string serial, uint64_t* transport_id);
	static std::future<CommandExecution::CommandResult> run_async(const std::string& command);
	static std::string run(const std::string& command, bool& out_isSuccess);
	static std::string run(const std::string& command);
	static std::vector<std::string> DetectDevices();
	static bool InstallAPK(const std::string& deviceId, std::vector<std::string> apks, bool force = false);
	~AdbClient();

	bool isAdbServerRunning();
	bool startAdbServer();
	bool stopAdbServer();
	std::vector<std::string> getConnectedDevices();
	std::vector<DeviceInfo> getConnectedDevicesInfo(); // 测试一下速度会不会变慢，会变慢多少
	bool adb_command(const std::string& service); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/adb_client.cpp#374
	bool adb_query(const std::string& service, std::string* result, std::string* error, bool force_switch_device = false); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/adb_client.cpp#391
	std::string adb_query(const std::string& service, bool force_switch_device=false);
	int adb_connect(std::string service, std::string* error); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/adb_client.cpp#238
	int adb_shell(std::vector<std::string> cmds, std::string* result, std::string* error);
	int adb_remote_shell(std::vector<std::string> cmds, std::string* result, std::string* error);
	bool reboot(int mod,std::string* error);
	static bool ReadFdExactly(SOCKET fd, void* buf, size_t len); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/adb_io.cpp?fi=SendProtocolString#76
	static bool WriteFdExactly(SOCKET fd, const void* buf, size_t len); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/adb_io.cpp?fi=SendProtocolString#103
	bool adb_push(std::vector<std::string> local_path, std::string remote_path, std::string& error); // 这个复杂，先画个饼

private:
	static const int ADB_PORT = 5037; // adb的默认端口
	static const std::string ADB_HOST; // 默认的本机连接
	WSADATA wsaData;
	bool wsaInitialized = false;
	//SOCKET m_sock; // 内部维护的sock对象，不能这么写，其内部会创建多个socket用来连接，都是独立的
	uint64_t* m_transport_id;
	std::string m_serial;


	bool initializeWsa();
	void cleanupWsa();
	bool createAndConnectSocket(SOCKET& m_sock);
	bool isProcessRunning(const WCHAR* processName);
	bool adb_status(SOCKET& m_sock, std::string* error); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/adb_client.cpp#137
	std::optional<uint64_t> switch_socket_transport(SOCKET& m_sock, std::string* error); // 适用于多设备 http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/adb_client.cpp#81
	bool SendProtocolString(SOCKET& m_sock, const std::string& s, std::string* error); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/adb_io.cpp?fi=SendProtocolString#37
	bool ReadProtocolString(SOCKET& m_sock, std::string* s, std::string* error); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/adb_io.cpp?fi=SendProtocolString#50
	int _adb_connect(std::string service, uint64_t* transport, std::string* error, bool force_switch = false); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/adb_client.cpp#158
	int adb_connect(uint64_t* transport, std::string service, std::string* error, bool force_switch_device = false); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/adb_client.cpp#349
	int read_and_dump(SOCKET fd,bool use_shell_protocol, std::string& result); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/commandline.cpp#317
	int read_and_dump_protocol(SOCKET fd, std::string &out, size_t& out_size, std::string& err, size_t& err_size); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/commandline.cpp#read_and_dump_protocol
	std::vector<std::string> adb_get_feature(std::string* error);
};



std::string to_string(ConnectionState state);
ConnectionState toConnectionState(const std::string& state);