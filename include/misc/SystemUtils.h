#pragma once
#include <Windows.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <sys/stat.h>

/* �ļ��������� */
#define S_IFMT  00170000

/* �ļ����� */
#define S_IFSOCK 0140000 /* socket */
#define S_IFLNK  0120000 /* symbolic link */
#define S_IFREG  0100000 /* regular file */
#define S_IFBLK  0060000 /* block device */
#define S_IFDIR  0040000 /* directory */
#define S_IFCHR  0020000 /* character device */
#define S_IFIFO  0010000 /* FIFO */
/* �ļ����ͼ��� */
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)  /* �Ƿ�Ϊ�������� */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)  /* �Ƿ�Ϊ�����ļ� */
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)  /* �Ƿ�ΪĿ¼ */
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)  /* �Ƿ�Ϊ�ַ��豸 */
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)  /* �Ƿ�Ϊ���豸 */
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)  /* �Ƿ�ΪFIFO/�ܵ� */
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK) /* �Ƿ�Ϊ�׽��� */

/* Ȩ��λ */
#define S_IRWXU 00700 /* �����߶�дִ��Ȩ�� */
#define S_IRUSR 00400 /* �����߶�Ȩ�� */
#define S_IWUSR 00200 /* ������дȨ�� */
#define S_IXUSR 00100 /* ������ִ��Ȩ�� */

#define S_IRWXG 00070 /* ���дִ��Ȩ�� */
#define S_IRGRP 00040 /* ���Ȩ�� */
#define S_IWGRP 00020 /* ��дȨ�� */
#define S_IXGRP 00010 /* ��ִ��Ȩ�� */

#define S_IRWXO 00007 /* �����˶�дִ��Ȩ�� */
#define S_IROTH 00004 /* �����˶�Ȩ�� */
#define S_IWOTH 00002 /* ������дȨ�� */
#define S_IXOTH 00001 /* ������ִ��Ȩ�� */

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
// ����ӳ����i64�汾��stat������֧�ִ���2G���ļ���adbԴ������ʹ����MinGW�����岻ͬ��llvm-mingw�п�����stat�ǲ�֧�ִ���2G�ļ��ģ�
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
// ʹ�� glibc �� mempcpy
#else
// ʹ���Զ���ʵ��
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
