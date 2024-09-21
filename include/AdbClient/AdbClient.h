#pragma once

#include "CommandExecution/CommandExecution.h"
#include <optional>
#include <cctype>
#include "AdbClient/file_sync_protocol.h"

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

struct DeviceInfo {
    std::string serial;
    ConnectionState deviceState;
    std::string devpath;
    std::string product;
    std::string model;
    std::string device;
    std::string transport_id;

    static std::string to_string(ConnectionState state) {
        switch (state) {
        case kCsOffline:
            return "offline";
        case kCsBootloader:
            return "bootloader";
        case kCsDevice:
            return "device";
        case kCsHost:
            return "host";
        case kCsRecovery:
            return "recovery";
        case kCsRescue:
            return "rescue";
        case kCsNoPerm:
            return "no permissions (unknow); see [http://developer.android.com/tools/device.html]"; // emmmmmm
        case kCsSideload:
            return "sideload";
        case kCsUnauthorized:
            return "unauthorized";
        case kCsAuthorizing:
            return "authorizing";
        case kCsConnecting:
            return "connecting";
        default:
            return "unknown";
        }
    }

    static ConnectionState toConnectionState(const std::string& state) {
        std::string lowerState = state;
        std::transform(lowerState.begin(), lowerState.end(), lowerState.begin(),
            [](unsigned char c) { return std::tolower(c); });

        if (lowerState == "offline") {
            return kCsOffline;
        }
        else if (lowerState == "bootloader") {
            return kCsBootloader;
        }
        else if (lowerState == "device") {
            return kCsDevice;
        }
        else if (lowerState == "host") {
            return kCsHost;
        }
        else if (lowerState == "recovery") {
            return kCsRecovery;
        }
        else if (lowerState == "rescue") {
            return kCsRescue;
        }
        else if (lowerState == "no permissions (unknown); see [http://developer.android.com/tools/device.html]") {
            return kCsNoPerm;
        }
        else if (lowerState == "sideload") {
            return kCsSideload;
        }
        else if (lowerState == "unauthorized") {
            return kCsUnauthorized;
        }
        else if (lowerState == "authorizing") {
            return kCsAuthorizing;
        }
        else if (lowerState == "connecting") {
            return kCsConnecting;
        }
        else {
            return kCsUnknown;
        }
    }

};

typedef DeviceInfo DeviceInfo;

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
	int adb_shell(std::vector<std::string> cmds, std::string* result, std::string* error);
	int adb_remote_shell(std::vector<std::string> cmds, std::string* result, std::string* error);
	bool reboot(int mod,std::string* error);
	
    bool AdbClient::adb_push(std::vector<const char*> local_path, const char* remote_path, bool sync, CompressionType compression, bool dry_run, std::string& error); // 这个复杂，先画个饼


	bool adb_query(const std::string& service, std::string* result, std::string* error, bool force_switch_device = false);
	std::string adb_query(const std::string& service, bool force_switch_device = false);
	bool adb_command(const std::string& service);
	int adb_connect(uint64_t* transport, std::string service, std::string* error, bool force_switch_device = false);
	int adb_connect(std::string service, std::string* error);

	// 弄一个exec函数，似乎是用来执行使用原始二进制执行命令的



private:
	static const int ADB_PORT = 5037; // adb的默认端口
	static const std::string ADB_HOST; // 默认的本机连接
	WSADATA wsaData;
	bool wsaInitialized = false;
	uint64_t* m_transport_id;
	std::string m_serial;
	std::vector<std::string> features;



	bool initializeWsa();
	void cleanupWsa();

	
};