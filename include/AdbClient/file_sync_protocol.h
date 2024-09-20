// http://aospxref.com/android-13.0.0_r3/raw/packages/modules/adb/file_sync_protocol.h
#pragma once
#include <cstdint>
//#include <winerror.h>
//#include <Winbase.h>
//#include<Windows.h>

#define MKID(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((d) << 24))

#define ID_LSTAT_V1 MKID('S', 'T', 'A', 'T')
#define ID_STAT_V2 MKID('S', 'T', 'A', '2')
#define ID_LSTAT_V2 MKID('L', 'S', 'T', '2')

#define ID_LIST_V1 MKID('L', 'I', 'S', 'T')
#define ID_LIST_V2 MKID('L', 'I', 'S', '2')
#define ID_DENT_V1 MKID('D', 'E', 'N', 'T')
#define ID_DENT_V2 MKID('D', 'N', 'T', '2')

#define ID_SEND_V1 MKID('S', 'E', 'N', 'D')
#define ID_SEND_V2 MKID('S', 'N', 'D', '2')
#define ID_RECV_V1 MKID('R', 'E', 'C', 'V')
#define ID_RECV_V2 MKID('R', 'C', 'V', '2')
#define ID_DONE MKID('D', 'O', 'N', 'E')
#define ID_DATA MKID('D', 'A', 'T', 'A')
#define ID_OKAY MKID('O', 'K', 'A', 'Y')
#define ID_FAIL MKID('F', 'A', 'I', 'L')
#define ID_QUIT MKID('Q', 'U', 'I', 'T')

struct SyncRequest {
    uint32_t id;           // ID_STAT, et cetera.
    uint32_t path_length;  // <= 1024
    // Followed by 'path_length' bytes of path (not NUL-terminated).
} __attribute__((packed));

struct __attribute__((packed)) sync_stat_v1 {
    uint32_t id;
    uint32_t mode;
    uint32_t size;
    uint32_t mtime;
};

struct __attribute__((packed)) sync_stat_v2 {
    uint32_t id;
    uint32_t error;
    uint64_t dev;
    uint64_t ino;
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    int64_t atime;
    int64_t mtime;
    int64_t ctime;
};

struct __attribute__((packed)) sync_dent_v1 {
    uint32_t id;
    uint32_t mode;
    uint32_t size;
    uint32_t mtime;
    uint32_t namelen;
};  // followed by `namelen` bytes of the name.

struct __attribute__((packed)) sync_dent_v2 {
    uint32_t id;
    uint32_t error;
    uint64_t dev;
    uint64_t ino;
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    int64_t atime;
    int64_t mtime;
    int64_t ctime;
    uint32_t namelen;
};  // followed by `namelen` bytes of the name.

enum SyncFlag : uint32_t {
    kSyncFlagNone = 0,
    kSyncFlagBrotli = 1,
    kSyncFlagLZ4 = 2,
    kSyncFlagZstd = 4,
    kSyncFlagDryRun = 0x8000'0000U,
};

enum class CompressionType {
    None,
    Any,
    Brotli,
    LZ4,
    Zstd,
};

// send_v1 sent the path in a buffer, followed by a comma and the mode as a string.
// send_v2 sends just the path in the first request, and then sends another syncmsg (with the
// same ID!) with details.
struct __attribute__((packed)) sync_send_v2 {
    uint32_t id;
    uint32_t mode;
    uint32_t flags;
};

// Likewise, recv_v1 just sent the path without any accompanying data.
struct __attribute__((packed)) sync_recv_v2 {
    uint32_t id;
    uint32_t flags;
};

struct __attribute__((packed)) sync_data {
    uint32_t id;
    uint32_t size;
};  // followed by `size` bytes of data.

struct __attribute__((packed)) sync_status {
    uint32_t id;
    uint32_t msglen;
};  // followed by `msglen` bytes of error message, if id == ID_FAIL.

union syncmsg {
    sync_stat_v1 stat_v1;
    sync_stat_v2 stat_v2;
    sync_dent_v1 dent_v1;
    sync_dent_v2 dent_v2;
    sync_data data;
    sync_status status;
    sync_send_v2 send_v2_setup;
    sync_recv_v2 recv_v2_setup;
};

#define SYNC_DATA_MAX (64 * 1024)

//static uint32_t windows_attributes_to_mode(DWORD attrs, const char* path) {
//    uint32_t mode = 0;
//
//    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
//        mode |= S_IFDIR;
//    else
//        mode |= S_IFREG;
//
//    // 设置基本权限
//    mode |= S_IRUSR | S_IRGRP | S_IROTH;
//
//    if (!(attrs & FILE_ATTRIBUTE_READONLY))
//        mode |= S_IWUSR | S_IWGRP | S_IWOTH;
//
//    // 检查文件扩展名来判断是否可执行
//    const char* ext = strrchr(path, '.');
//    if (ext && (_stricmp(ext, ".exe") == 0 || _stricmp(ext, ".bat") == 0 || _stricmp(ext, ".cmd") == 0)) {
//        mode |= S_IXUSR | S_IXGRP | S_IXOTH;
//    }
//
//    // 目录总是有执行权限
//    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
//        mode |= S_IXUSR | S_IXGRP | S_IXOTH;
//
//    return mode;
//}
//
//int adb_stat(const char* path, struct adb_stat* s) {
//    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
//    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fileInfo)) {
//        switch (GetLastError()) {
//        case ERROR_FILE_NOT_FOUND:
//        case ERROR_PATH_NOT_FOUND:
//            errno = ENOENT;
//            break;
//        case ERROR_ACCESS_DENIED:
//            errno = EACCES;
//            break;
//        default:
//            errno = EIO;
//        }
//        return -1;
//    }
//
//    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
//    if (hFile == INVALID_HANDLE_VALUE) {
//        errno = EACCES;
//        return -1;
//    }
//
//    BY_HANDLE_FILE_INFORMATION fileInformation;
//    if (!GetFileInformationByHandle(hFile, &fileInformation)) {
//        CloseHandle(hFile);
//        errno = EIO;
//        return -1;
//    }
//
//    s->st_dev = fileInformation.dwVolumeSerialNumber;
//    s->st_ino = (static_cast<uint64_t>(fileInformation.nFileIndexHigh) << 32) | fileInformation.nFileIndexLow;
//    s->st_mode = windows_attributes_to_mode(fileInfo.dwFileAttributes, path);
//    s->st_nlink = fileInformation.nNumberOfLinks;
//    s->st_uid = 0;
//    s->st_gid = 0;
//    s->st_size = (static_cast<uint64_t>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;
//
//    // 转换时间
//    ULARGE_INTEGER uli;
//    uli.LowPart = fileInfo.ftLastAccessTime.dwLowDateTime;
//    uli.HighPart = fileInfo.ftLastAccessTime.dwHighDateTime;
//    s->st_atime = uli.QuadPart / 10000000ULL - 11644473600ULL;
//
//    uli.LowPart = fileInfo.ftLastWriteTime.dwLowDateTime;
//    uli.HighPart = fileInfo.ftLastWriteTime.dwHighDateTime;
//    s->st_mtime = uli.QuadPart / 10000000ULL - 11644473600ULL;
//
//    uli.LowPart = fileInfo.ftCreationTime.dwLowDateTime;
//    uli.HighPart = fileInfo.ftCreationTime.dwHighDateTime;
//    s->st_ctime = uli.QuadPart / 10000000ULL - 11644473600ULL;
//
//    CloseHandle(hFile);
//    return 0;
//}