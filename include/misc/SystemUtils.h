#pragma once
#include <Windows.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <sys/stat.h>

/* 文件类型掩码 */
#define S_IFMT  00170000

/* 文件类型 */
#define S_IFSOCK 0140000 /* socket */
#define S_IFLNK  0120000 /* symbolic link */
#define S_IFREG  0100000 /* regular file */
#define S_IFBLK  0060000 /* block device */
#define S_IFDIR  0040000 /* directory */
#define S_IFCHR  0020000 /* character device */
#define S_IFIFO  0010000 /* FIFO */
/* 文件类型检查宏 */
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)  /* 是否为符号链接 */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)  /* 是否为常规文件 */
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)  /* 是否为目录 */
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)  /* 是否为字符设备 */
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)  /* 是否为块设备 */
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)  /* 是否为FIFO/管道 */
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK) /* 是否为套接字 */

/* 权限位 */
#define S_IRWXU 00700 /* 所有者读写执行权限 */
#define S_IRUSR 00400 /* 所有者读权限 */
#define S_IWUSR 00200 /* 所有者写权限 */
#define S_IXUSR 00100 /* 所有者执行权限 */

#define S_IRWXG 00070 /* 组读写执行权限 */
#define S_IRGRP 00040 /* 组读权限 */
#define S_IWGRP 00020 /* 组写权限 */
#define S_IXGRP 00010 /* 组执行权限 */

#define S_IRWXO 00007 /* 其他人读写执行权限 */
#define S_IROTH 00004 /* 其他人读权限 */
#define S_IWOTH 00002 /* 其他人写权限 */
#define S_IXOTH 00001 /* 其他人执行权限 */

// http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/sysdeps/win32/stat.cpp
// stat is broken on Win32: stat on a path with a trailing slash or backslash will always fail with
// ENOENT.
int adb_stat(const char* path, struct adb_stat* buf);

// We later define a macro mapping 'stat' to 'adb_stat'. This causes:
//   struct stat s;
//   stat(filename, &s);
// To turn into the following:
//   struct adb_stat s;
//   adb_stat(filename, &s);
// To get this to work, we need to make 'struct adb_stat' the same as
// 'struct stat'. Note that this definition of 'struct adb_stat' uses the
// *current* macro definition of stat, so it may actually be inheriting from
// struct _stat32i64 (or some other remapping).
// 这里映射了i64版本的stat，可以支持大于2G的文件，adb源代码中使用了MinGW，定义不同（llvm-mingw中看起来stat是不支持大于2G文件的）
struct adb_stat : public _stati64 {};
#undef stat
#define stat adb_stat
// Windows doesn't have lstat.
#define lstat adb_stat

struct DirectoryEntry {
    std::wstring d_name;
    bool d_type;
};

bool check_socket_status(int socket_fd, bool& is_readable, bool& is_writable, int timeout_sec);

bool isProcessRunning(const WCHAR* processName);

std::vector<DirectoryEntry> readDirectory(const std::wstring& path, std::wstring* error);

#ifdef __GLIBC__
// 使用 glibc 的 mempcpy
#else
// 使用自定义实现
void* mempcpy(void* dest, const void* src, size_t n);
#endif

HANDLE open(const char* path, int options, std::string* error);
int read(HANDLE f, void* buf, int len, std::string* error);

static constexpr DWORD kErrorMessageBufferSize = 256;
std::string SystemErrorCodeToString(int int_error_code);

struct adb_pollfd {
    SOCKET fd;
    short events;
    short revents;

};
extern int adb_poll(adb_pollfd* fds, size_t nfds, int timeout);
