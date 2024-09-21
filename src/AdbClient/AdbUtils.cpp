#include "AdbClient/AdbUtils.h"
#include "AdbClient/adb_io.h"
#include "misc/StringUtils.h"
#include "AdbClient/client.h"
#include <AdbClient/file_sync_client.h>
//#include "AdbClient/adb_types.h"
#include "misc/StringUtils.h"
#include <chrono>
#include <thread>



std::vector<std::string> adb_get_feature(std::string m_serial, uint64_t* m_transport_id, std::string* error)
{
    if (m_transport_id == nullptr && m_serial.empty())
    {
        *error = "";
        return {};
    }
    std::vector<std::string> features;
    std::string result;
    std::string prefix;
    if (m_transport_id != nullptr) prefix = StringPrintf("host-transport-id:%I64u:", *m_transport_id);
    else if (!m_serial.empty()) prefix = StringPrintf("host-serial:%s:", m_serial.c_str());
    else
    {
        *error = "";
        return {};
    }
    if (!BaseClient::adb_query(prefix + "features", m_serial, m_transport_id, &result, error)) return {};
    return Split(result, ",");
}

bool CanUseFeature(const std::vector<std::string>& feature_set, const  std::string& feature) {
    std::vector<std::string> supported_features = {
                kFeatureShell2,
                kFeatureCmd,
                kFeatureStat2,
                kFeatureLs2,
                kFeatureFixedPushMkdir,
                kFeatureApex,
                kFeatureAbb,
                kFeatureFixedPushSymlinkTimestamp,
                kFeatureAbbExec,
                kFeatureRemountShell,
                kFeatureTrackApp,
                kFeatureSendRecv2,
                kFeatureSendRecv2Brotli,
                kFeatureSendRecv2LZ4,
                kFeatureSendRecv2Zstd,
                kFeatureSendRecv2DryRunSend,
                kFeatureOpenscreenMdns,
                // Increment ADB_SERVER_VERSION when adding a feature that adbd needs
                // to know about. Otherwise, the client can be stuck running an old
                // version of the server even after upgrading their copy of adb.
                // (http://b/24370690)
    };
           // 检查设备实时有给定功能并且adb是否支持该功能
          return (CONTAINS(feature_set, feature)) && (CONTAINS(supported_features, feature));
    
}

// Returns a shell service string with the indicated arguments and command.
std::string ShellServiceString(bool use_shell_protocol, const std::string& type_arg, const std::string& command) {
    std::vector<std::string> args;
    if (use_shell_protocol) {
        args.push_back(kShellServiceArgShellProtocol);

        const char* terminal_type = getenv("TERM");
        if (terminal_type != nullptr) {
            args.push_back(std::string("TERM=") + terminal_type);
        }
    }
    if (!type_arg.empty()) {
        args.push_back(type_arg);
    }

    // Shell service string can look like: shell[,arg1,arg2,...]:[command].
    return StringPrintf("shell%s%s:%s",
        args.empty() ? "" : ",",
        Join(args, ',').c_str(),
        command.c_str());
}

std::string format_host_command(std::string m_serial, uint64_t* m_transport_id, const char* command) {
    if (m_transport_id) {
        return StringPrintf("host-transport-id:%I64o:%s", m_transport_id,command);
    }
    else if (!m_serial.empty()) {
        return StringPrintf("host-serial:%s:%s", m_serial.c_str(), command);
    }

    const char* prefix = "host";
    return StringPrintf("%s:%s", prefix, command);
}

int errno_from_wire(int wire_error) {
    switch (wire_error) {
    case 13: return EACCES;
    case 17: return EEXIST;
    case 14: return EFAULT;
    case 27: return EFBIG;
    case 4:  return EINTR;
    case 22: return EINVAL;
    case 5:  return EIO;
    case 21: return EISDIR;
    case 40: return ELOOP;
    case 24: return EMFILE;
    case 36: return ENAMETOOLONG;
    case 23: return ENFILE;
    case 2:  return ENOENT;
    case 12: return ENOMEM;
    case 28: return ENOSPC;
    case 20: return ENOTDIR;
    case 75: return EOVERFLOW;
    case 1:  return EPERM;
    case 30: return EROFS;
    case 26: return ETXTBSY;
    default: return 0; // 默认情况下返回0，表示没有匹配的错误
    }
}