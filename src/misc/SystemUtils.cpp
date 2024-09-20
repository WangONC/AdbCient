
#include <WinSock2.h>
#include <TlHelp32.h>
#include <tchar.h>
#include <iostream>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "misc/SystemUtils.h"
#include "misc/StringUtils.h"




bool check_socket_status(int socket_fd, bool& is_readable, bool& is_writable, int timeout_sec) {
    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_SET(socket_fd, &read_fds);
    FD_SET(socket_fd, &write_fds);

    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    int ready = select(socket_fd + 1, &read_fds, &write_fds, NULL, &tv);
    if (ready == -1) {
        std::cerr << "Error in select" << std::endl;
        return false;
    }
    else if (ready == 0) {
        std::cout << "Timeout occurred, no data available" << std::endl;
        return false;
    }
    else {
        is_readable = FD_ISSET(socket_fd, &read_fds);
        is_writable = FD_ISSET(socket_fd, &write_fds);
        return true;
    }
}

// 检测某个进程是否再运行，或许可以放到什么工具类里面
bool isProcessRunning(const WCHAR* processName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(hSnapshot, &pe32)) {
        CloseHandle(hSnapshot);
        return false;
    }

    do {
        if (_tcscmp(pe32.szExeFile, processName) == 0) {
            CloseHandle(hSnapshot);
            return true;
        }
    } while (Process32Next(hSnapshot, &pe32));

    CloseHandle(hSnapshot);
    return false;
}

// Version of stat() that takes a UTF-8 path.
int adb_stat(const char* path, struct adb_stat* s) {
    // This definition of wstat seems to be missing from <sys/stat.h>.
#ifdef _USE_32BIT_TIME_T
#define wstat _wstat32i64
#else
#define wstat _wstat64
#endif

    std::wstring path_wide = CharToWstring(path);


    // If the path has a trailing slash, stat will fail with ENOENT regardless of whether the path
    // is a directory or not.
    bool expected_directory = false;
    while (*path_wide.rbegin() == u'/' || *path_wide.rbegin() == u'\\') {
        path_wide.pop_back();
        expected_directory = true;
    }

    struct adb_stat st;
    int result = wstat(path_wide.c_str(), &st);
    if (result == 0 && expected_directory) {
        if (!S_ISDIR(st.st_mode)) {
            errno = ENOTDIR;
            return -1;
        }
    }

    memcpy(s, &st, sizeof(st));
    return result;
}

std::vector<DirectoryEntry> readDirectory(const std::wstring& path, std::wstring* error) {
    std::vector<DirectoryEntry> entries;
    if (path.empty())
    {
        errno = ENOENT;
        return entries;
    }
    DWORD n;
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    n = GetFullPathNameW(path.c_str(), 0, NULL, NULL);
#else
    n = wcslen(dirname);
#endif
    wchar_t* patt = (wchar_t*)malloc(sizeof(wchar_t) * n + 16);
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    n = GetFullPathNameW(path.c_str(), n, patt, NULL);
#else
    wcsncpy_s(patt, n + 1, dirname, n);
#endif

    wchar_t* p = patt + n;

    switch (p[-1]) {
    case '\\':
    case '/':
    case ':':
        /* Directory ends in path separator, e.g. c:\temp\ */
        /*NOP*/;
        break;

    default:
        /* Directory name doesn't end in path separator */
        *p++ = '\\';

    }
    *p++ = '*';
    *p = '\0';

    std::wstring search_path(patt);
    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(search_path.c_str(), &find_data);

    if (hFind == INVALID_HANDLE_VALUE) {
        *error = L"Error: Unable to open directory " + path + L"\n";
        return entries;
    }

    do {
        std::wstring filename = find_data.cFileName;
        DirectoryEntry entry;
        entry.d_name = filename;

        /* File type */
        DWORD attr = find_data.dwFileAttributes;
        if ((attr & FILE_ATTRIBUTE_DEVICE) != 0) { // 定义来自于 http://aospxref.com/android-13.0.0_r3/xref/external/libwebsockets/win32port/dirent/dirent-win32.h?r=&mo=9967&fi=454#476
            entry.d_type = S_IFCHR;
        }
        else if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            entry.d_type = S_IFDIR;
        }
        else {
            entry.d_type = S_IFREG;
        }
        //entry.is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entries.push_back(entry);
    } while (FindNextFileW(hFind, &find_data) != 0);

    DWORD dwError = GetLastError();
    if (dwError != ERROR_NO_MORE_FILES) {
        *error = L"Error: FindNextFile error. Error is " + std::to_wstring(dwError) + L"\n";
    }

    FindClose(hFind);
    return entries;
}

#ifdef __GLIBC__
// 使用 glibc 的 mempcpy
#else
// 使用自定义实现
void* mempcpy(void* dest, const void* src, size_t n) {
    return (char*)memcpy(dest, src, n) + n;
}
#endif

// 以后可能要考虑跨平台
HANDLE open(const char* path, int options,std::string *error) {

    DWORD desiredAccess = 0;
    DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;

    // CreateFileW is inherently O_CLOEXEC by default.
    //options &= ~O_CLOEXEC;

    std::wstring path_wide  = CharToWstring(path);

    HANDLE fh_handle = CreateFileW(path_wide.c_str(), desiredAccess, shareMode, nullptr, OPEN_EXISTING, 0, nullptr);

    if (fh_handle == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        //_fh_close(f);
        CloseHandle(fh_handle);
        *error = StringPrintf("open: could not open '%s': ", path);
        switch (err) {
        case ERROR_FILE_NOT_FOUND:
            *error = StringPrintf("file not found");
            errno = ENOENT;
            return INVALID_HANDLE_VALUE;

        case ERROR_PATH_NOT_FOUND:
            *error = StringPrintf("path not found");
            errno = ENOTDIR;
            return INVALID_HANDLE_VALUE;

        default:
            *error = StringPrintf("unknown error: %s", SystemErrorCodeToString(err).c_str());
            errno = ENOENT;
            return INVALID_HANDLE_VALUE;
        }
    }

    return fh_handle;
}

int read(HANDLE f, void* buf, int len,std::string *error) {
    DWORD read_bytes;

    if (!ReadFile(f, buf, (DWORD)len, &read_bytes, nullptr)) {
        *error = StringPrintf("read: could not read %d bytes", len);
        errno = EIO;
        return -1;
    }
    return read_bytes;
}

std::string SystemErrorCodeToString(int int_error_code) {
    WCHAR msgbuf[kErrorMessageBufferSize];
    DWORD error_code = int_error_code;
    DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageW(flags, nullptr, error_code, 0, msgbuf,
        kErrorMessageBufferSize, nullptr);
    if (len == 0) {
        return StringPrintf(
            "Error %lu while retrieving message for error %lu", GetLastError(),
            error_code);
    }

    // Convert UTF-16 to UTF-8.
    std::string msg  = WstringToString(std::wstring(msgbuf));


    // Messages returned by the system end with line breaks.
    msg = Trim(msg);

    // There are many Windows error messages compared to POSIX, so include the
    // numeric error code for easier, quicker, accurate identification. Use
    // decimal instead of hex because there are decimal ranges like 10000-11999
    // for Winsock.
    StringAppendF(&msg, " (%lu)", error_code);
    return msg;
}

bool ReadFdToString(HANDLE fd, std::string* content) {
    content->clear();

    LARGE_INTEGER fileSize;
    if (GetFileSizeEx(fd, &fileSize)) {
        // 确保文件大小不超过 size_t 的最大值
        if (fileSize.QuadPart > static_cast<LONGLONG>(SIZE_MAX)) {
            errno = EFBIG;  // 文件太大，无法处理
            return false;
        }
        // 预留空间
        content->reserve(static_cast<size_t>(fileSize.QuadPart));
    }

    // 读取文件内容
    char buf[BUFSIZ];
    DWORD n;
    while (ReadFile(fd, buf, sizeof(buf), &n, nullptr) && n > 0) {
        content->append(buf, n);
    }

    // 检查读取是否成功
    return (n == 0) ? true : false;
}

bool ReadFileToString(const std::string& path, std::string* content, bool follow_symlinks,std::string *error) {
    content->clear();

    //int flags = O_RDONLY | O_CLOEXEC | O_BINARY | (follow_symlinks ? 0 : O_NOFOLLOW);
    HANDLE fd(open(path.c_str(), GENERIC_READ, error));
    if (fd == INVALID_HANDLE_VALUE) {
        return false;
    }
    return ReadFdToString(fd, content);
}

void _socket_set_errno(const DWORD err) {
    // Because the Windows C Runtime (MSVCRT.DLL) strerror() does not support a
    // lot of POSIX and socket error codes, some of the resulting error codes
    // are mapped to strings by adb_strerror().
    switch (err) {
    case 0:              errno = 0; break;
        // Don't map WSAEINTR since that is only for Winsock 1.1 which we don't use.
        // case WSAEINTR:    errno = EINTR; break;
    case WSAEFAULT:      errno = EFAULT; break;
    case WSAEINVAL:      errno = EINVAL; break;
    case WSAEMFILE:      errno = EMFILE; break;
        // Mapping WSAEWOULDBLOCK to EAGAIN is absolutely critical because
        // non-blocking sockets can cause an error code of WSAEWOULDBLOCK and
        // callers check specifically for EAGAIN.
    case WSAEWOULDBLOCK: errno = EAGAIN; break;
    case WSAENOTSOCK:    errno = ENOTSOCK; break;
    case WSAENOPROTOOPT: errno = ENOPROTOOPT; break;
    case WSAEOPNOTSUPP:  errno = EOPNOTSUPP; break;
    case WSAENETDOWN:    errno = ENETDOWN; break;
    case WSAENETRESET:   errno = ENETRESET; break;
        // Map WSAECONNABORTED to EPIPE instead of ECONNABORTED because POSIX seems
        // to use EPIPE for these situations and there are some callers that look
        // for EPIPE.
    case WSAECONNABORTED: errno = EPIPE; break;
    case WSAECONNRESET:  errno = ECONNRESET; break;
    case WSAENOBUFS:     errno = ENOBUFS; break;
    case WSAENOTCONN:    errno = ENOTCONN; break;
        // Don't map WSAETIMEDOUT because we don't currently use SO_RCVTIMEO or
        // SO_SNDTIMEO which would cause WSAETIMEDOUT to be returned. Future
        // considerations: Reportedly send() can return zero on timeout, and POSIX
        // code may expect EAGAIN instead of ETIMEDOUT on timeout.
        // case WSAETIMEDOUT: errno = ETIMEDOUT; break;
    case WSAEHOSTUNREACH: errno = EHOSTUNREACH; break;
    default:
        errno = EINVAL;
    }
}

extern int adb_poll(adb_pollfd* fds, size_t nfds, int timeout) {
    // WSAPoll doesn't handle invalid/non-socket handles, so we need to handle them ourselves.
    int skipped = 0;
    std::vector<WSAPOLLFD> sockets;
    std::vector<adb_pollfd*> original;

    for (size_t i = 0; i < nfds; ++i) {
        sockaddr_in addr;
        int addr_len = sizeof(addr);
        

        WSAPOLLFD wsapollfd = {
            .fd = fds[i].fd,
            .events = static_cast<short>(fds[i].events)
        };
        sockets.push_back(wsapollfd);
        original.push_back(&fds[i]);
        
    }

    if (sockets.empty()) {
        return skipped;
    }

    // If we have any invalid FDs in our FD set, make sure to return immediately.
    if (skipped > 0) {
        timeout = 0;
    }

    int result = WSAPoll(sockets.data(), sockets.size(), timeout);
    if (result == SOCKET_ERROR) {
        _socket_set_errno(WSAGetLastError());
        return -1;
    }

    // Map the results back onto the original set.
    for (size_t i = 0; i < sockets.size(); ++i) {
        original[i]->revents = sockets[i].revents;
    }

    // WSAPoll appears to return the number of unique FDs with available events, instead of how many
    // of the pollfd elements have a non-zero revents field, which is what it and poll are specified
    // to do. Ignore its result and calculate the proper return value.
    result = 0;
    for (size_t i = 0; i < nfds; ++i) {
        if (fds[i].revents != 0) {
            ++result;
        }
    }
    return result;
}