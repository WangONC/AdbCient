#include "AdbClient/AdbClient.h"
#include "AdbClient/adb_io.h"
#include <fstream>
#include <iostream>
#include <windows.h>
#include <winsock2.h>

#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>
#include "AdbClient/ShellProtocol.h"
#include "misc/SystemUtils.h"
#include "AdbClient/client.h"
#include "AdbClient/file_sync_client.h"
#include "AdbClient/AdbUtils.h"
#pragma comment(lib, "ws2_32.lib")




AdbClient::AdbClient()
{
    m_serial = "";
    m_transport_id = nullptr;
    if (!initializeWsa()) {
        throw std::runtime_error("WSAStartup failed");
    }
    std::vector<DeviceInfo> di = this->getConnectedDevicesInfo();
    if (di.size() == 1)// 没有手动指定设备的情况下，只有一个设备才会自动获取，为了避免自动获取某个设备造成混淆，并且最大限度模仿原生adb行为
    {
        m_serial = di[0].serial;
        *m_transport_id = std::stoull(di[0].transport_id);
        std::string *error;
        features = adb_get_feature(m_serial, m_transport_id, error);
        if (features.empty())
        {
            throw std::runtime_error("failed to get feature set: " + *error);
        }
    }


}

AdbClient::AdbClient(std::string serial)
{
    m_serial = serial;
    m_transport_id = nullptr;
    if (!initializeWsa()) {
        throw std::runtime_error("WSAStartup failed");
    }
    std::string error;
    features = adb_get_feature(m_serial, m_transport_id, &error);
    if (features.empty())
    {
        throw std::runtime_error("failed to get feature set: " + error);
    }
}

AdbClient::AdbClient(uint64_t* transport_id)
{
    m_serial = "";
    m_transport_id = transport_id;
    if (!initializeWsa()) {
        throw std::runtime_error("WSAStartup failed");
    }
    std::string error;
    features = adb_get_feature(m_serial, m_transport_id, &error);
    if (features.empty())
    {
        throw std::runtime_error("failed to get feature set: " + error);
    }
}

AdbClient::AdbClient(std::string serial, uint64_t* transport_id)
{
    m_serial = serial;
    m_transport_id = transport_id;
    if (!initializeWsa()) {
        throw std::runtime_error("WSAStartup failed");
    }
    std::string error;
    features = adb_get_feature(m_serial, m_transport_id, &error);
    if (features.empty())
    {
        throw std::runtime_error("failed to get feature set: " + error);
    }
}

AdbClient::~AdbClient()
{
    cleanupWsa();
    //closesocket(m_sock);
}

// 终端命令执行的方式（powershell）异步执行adb命令
std::future<CommandExecution::CommandResult> AdbClient::run_async(const std::string& command)
{
    return CommandExecution::ExecCommandAsync("adb.exe " + command);
}

// 终端命令执行的方式（powershell）阻塞的执行adb命令
std::string AdbClient::run(const std::string& command,bool& out_isSuccess)
{
    // 使用 CommandExecution 类的同步执行命令方法
    auto result = CommandExecution::ExecCommand("adb.exe " + command);
    out_isSuccess = result.result == 0;
    if (!out_isSuccess) {
        return result.errorMsg;
    }
    return result.successMsg;
}

// 终端命令执行的方式（powershell）阻塞的执行adb命令
std::string AdbClient::run(const std::string& command)
{
    // 使用 CommandExecution 类的同步执行命令方法
    auto result = CommandExecution::ExecCommand("adb.exe " + command);
    if (result.result != 0) {
        return result.errorMsg;
    }
    return result.successMsg;
}

// 通过执行终端命令的方式获取连接的设备，稍慢
std::vector<std::string> AdbClient::DetectDevices()
{
    std::vector<std::string> m_devices;
    std::string output = AdbClient::run("devices");

    std::istringstream iss(output);
    std::string line;

    while (std::getline(iss, line)) {
        size_t tabPos = line.find('\t');
        if (tabPos != std::string::npos) {
            std::string deviceId = line.substr(0, tabPos);
            m_devices.push_back(deviceId);
        }
    }
    return m_devices;
}

// 通过终端命令执行异步安装多个apk，不阻塞，不关心是否成功
bool AdbClient::InstallAPK(const std::string& deviceId, std::vector<std::string> apks, bool force)
{
    if (deviceId.empty()) {
        return false;
    }

    std::ostringstream commandStream;
    commandStream << "-s " << deviceId << " install-multi-package";
    if (force) commandStream << " -r -d -t"; // 强制安装，允许覆盖、降级、测试签

    
    bool hasValidApk = false;
    for (auto apk : apks)
    {
        std::ifstream file(apk);
        if (!file.good()) continue;
        // adb只支持.apk和.apex
        std::string lowerApk = apk;
        std::transform(lowerApk.begin(), lowerApk.end(), lowerApk.begin(), ::tolower);
        if (lowerApk.substr(lowerApk.length() - 4) != ".apk" && lowerApk.substr(lowerApk.length() - 5) != ".apex") continue;
        commandStream << " \"" << apk << "\"";
        hasValidApk = true;
    }
    // 确保至少有一个有效的 APK 文件，可以排除掉选择多个文件包含了apk但不都是apk的情况
    if (!hasValidApk)  return false;

    // 异步执行命令
    run_async(commandStream.str());

    return true;
}

// 初始化 Winsock 
bool AdbClient::initializeWsa() {
    if (!wsaInitialized) {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed" << std::endl;
            return false;
        }
        wsaInitialized = true;
    }
    return true;
}

// 清理 Winsock 
void AdbClient::cleanupWsa() {
    if (wsaInitialized) {
        WSACleanup();
        wsaInitialized = false;
    }
}


bool AdbClient::adb_push(std::vector<const char*> local_path, const char* remote_path, std::string &error)
{
    bool copy_attrs = false;
    bool sync = false;
    bool dry_run = false;
    CompressionType compression = CompressionType::Any;
    return do_sync_push(m_serial, m_transport_id, local_path, remote_path, false, compression, false, &error);


   
    //return false;
}





// 检测adb server是否再运行的封装接口
bool AdbClient::isAdbServerRunning() {
    return isProcessRunning(_T("adb.exe"));
}
 
// 阻塞启动adb server，实际上是直接执行adb.exe start-server
bool AdbClient::startAdbServer() {
    if (isAdbServerRunning()) {
        return true;
    }

    bool isSuccess;
    run("start-server", isSuccess);
    if (!isSuccess) {
        std::cerr << "Failed to start ADB server" << std::endl;
        return false;
    }

    return isAdbServerRunning();
}

bool AdbClient::stopAdbServer() {
    std::string error;
    SOCKET fd(adb_connect("host::kill", &error));
    if (!error.empty()) return false; //这时候就有错误消息说明执行失败了
    bool r = BaseClient::adb_status(fd ,&error);
     closesocket(fd);
     return r;
}


std::vector<std::string> AdbClient::getConnectedDevices() {
    /*AdbResponse response = sendCommand("host:devices");
    if (response.status != "OKAY") {
        return {};
    }*/
    std::string data,error;
    if (!adb_query("host:devices", &data, &error, false)) return {};

    std::vector<std::string> devices;

    // 处理整个响应字符串
    size_t start = 0;
    size_t end = data.find("\n");

    // 遍历每一行
    while (end != std::string::npos) {
        std::string device = data.substr(start, end - start);
        size_t tab_pos = device.find("\t");

        // 检查是否找到设备，并且设备状态是否为 "device"
        if (tab_pos != std::string::npos) {
            std::string device_serial = device.substr(0, tab_pos);
            std::string device_status = device.substr(tab_pos + 1);

            // 检查设备状态是否为有效状态（如 "device"）
            if (device_status.find("device") != std::string::npos) {
                devices.push_back(device_serial);
            }
        }

        // 移动到下一行
        start = end + 1;
        end = data.find("\n", start);
    }
    // 最后一行也有\n
    //// 检查最后一行（防止遗漏最后一行没有换行符的情况）
    //std::string last_device = response.data.substr(start);
    //size_t tab_pos = last_device.find("\t");
    //if (tab_pos != std::string::npos) {
    //    std::string device_serial = last_device.substr(0, tab_pos);
    //    std::string device_status = last_device.substr(tab_pos + 1);
    //    if (device_status.find("device") != std::string::npos) {
    //        devices.push_back(device_serial);
    //    }
    //}

    return devices;
}

std::vector<DeviceInfo> AdbClient::getConnectedDevicesInfo()
{
    /*AdbResponse response = sendCommand("host:devices-l");
    if (response.status != "OKAY") {
        return {};
    }*/

    std::string data,error;
    if (!adb_query("host:devices-l", &data, &error, false)) return {};

    std::vector<DeviceInfo> devices;

    size_t start = 0;
    size_t end = data.find("\n");
    while (end != std::string::npos) {
        DeviceInfo dInfo;
        std::string device = data.substr(start, end - start);
        
        size_t tab_pos = device.find_first_of(" "); // devices -l的格式化是%-22s %s"

        if (tab_pos != std::string::npos) {
            std::string device_serial = device.substr(0, tab_pos);
            dInfo.serial = (std::string)device_serial.c_str();
            std::string device_infostr;
            int r_start_pos;
            if(tab_pos >= 22 ) r_start_pos = tab_pos + 1; // 根据对齐定义
            else r_start_pos = 22 + 1;


            size_t status_pos = device.find_first_of(" ", r_start_pos);

            std::string status = device.substr(r_start_pos, status_pos - r_start_pos); // 序列号紧接着是状态
            dInfo.deviceState = DeviceInfo::toConnectionState((std::string)status.c_str());

            device_infostr = device.substr(status_pos + 1); // 截断后面的属性信息

            // 起始后面信息的顺序是固定的，如果真的有什么变更也可以不循环直接按顺序读
            size_t startinfo = 0;
            size_t space = device_infostr.find(" ");
            while (space != std::string::npos)
            {
                std::string info = device_infostr.substr(startinfo, space - startinfo);
                if (info.find(":") == std::string::npos) { // 没有:就是devpath,但一般都没有这个项目 append_transport_info(result, "", t->devpath, false);
                    dInfo.deviceState = DeviceInfo::toConnectionState((std::string)info.c_str());
                }
                

                else if (strcmp(info.substr(0, info.find(":")).c_str(), "product") == 0)
                    dInfo.product = (std::string)info.substr(info.find(":") + 1).c_str();
                else if (strcmp(info.substr(0, info.find(":")).c_str(), "model") == 0)
                    dInfo.model = (std::string)info.substr(info.find(":") + 1).c_str();
                else if (strcmp(info.substr(0, info.find(":")).c_str(), "device") == 0)
                    dInfo.device = (std::string)info.substr(info.find(":") + 1).c_str();
                else if (strcmp(info.substr(0, info.find(":")).c_str(), "transport_id") == 0)
                    dInfo.transport_id = (std::string)info.substr(info.find(":") + 1).c_str();


                startinfo = space + 1;
                space = device_infostr.find(" ", startinfo);
            }
            devices.push_back(dInfo);

           }

        start = end + 1;
        end = data.find("\n", start);
        }

    return devices;
}


// 这里采取多条编码并组合成一条的方式，这样可以支持几乎任意的复杂指令，并且也支持无输出命令
// 根据执行时间会有一定的阻塞，不适合阻塞敏感场景
int AdbClient::adb_shell(std::vector<std::string> cmds, std::string* result, std::string* error)
{
    if (cmds.empty()) {
        if (error) *error = "No commands provided";
        return -1;
    }
    // 编码命令,base64
    auto encode_command = [](const std::string& command) {
        std::string result;
        result.reserve(command.length() * 4 / 3 + 4);
        static const char base64_chars[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        int i = 0;
        int j = 0;
        unsigned char char_array_3[3];
        unsigned char char_array_4[4];

        for (char c : command) {
            char_array_3[i++] = c;
            if (i == 3) {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;

                for (i = 0; i < 4; i++)
                    result += base64_chars[char_array_4[i]];
                i = 0;
            }
        }

        if (i) {
            for (j = i; j < 3; j++)
                char_array_3[j] = '\0';

            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

            for (j = 0; j < i + 1; j++)
                result += base64_chars[char_array_4[j]];

            while (i++ < 3)
                result += '=';
        }

        return result;
        };

    // 组合命令
    std::ostringstream combined_cmd;
    combined_cmd << "shell";


    bool use_shell_protocol = false; // 是否使用v2版本
    //auto features = adb_get_feature(error);
    if (features.empty())
    {
        return -1;
    }
    for (auto f : features)
    {
        if (f == "shell_v2") use_shell_protocol = true;
    }
    if (use_shell_protocol) combined_cmd << ",v2";
    combined_cmd << ':';
    for (size_t i = 0; i < cmds.size(); ++i) {
        if (i > 0) combined_cmd << "; ";
        combined_cmd << "echo " << encode_command(cmds[i]) << " | base64 -d | sh";
    }

    // 执行命令
    SOCKET m_sock(adb_connect(nullptr, combined_cmd.str(), error));
    if (m_sock < 0) {
        return -1;
    }


    int ecode = read_and_dump(m_sock, use_shell_protocol, *result);
    // 这时候sock可能已经被关了

    closesocket(m_sock);
    return use_shell_protocol ? ecode: -2;
}

// 交互式终端连续执行命令，测试响应时间。如果支持v2则会自动使用v2
// 返回码是-2就意味着没有使用v2，意味着返回码不能用于判断执行结果，也不会有错误信息，-1意味着内部代码执行过程中出现了错误
// 在不考虑命令本身阻塞时间的情况下，比打包执行快很多（80%以上），但主要阻塞依旧在于终端的命令执行
int AdbClient::adb_remote_shell(std::vector<std::string> cmds, std::string* result, std::string* error)
{
    if (cmds.empty()) {
        if (error) *error = "No commands provided";
        return -1;
    }

    std::string shell = "shell,raw";
    bool use_shell_protocol = false; // 是否使用v2版本
    //auto features = adb_get_feature(error);
    if (features.empty())
    {
        return -1;
    }
    for (auto f : features)
    {
        if (f == "shell_v2") use_shell_protocol = true;
    }
    //use_shell_protocol = false;
    // 这里如果使用-x，就是默认的shell:，在终端中会完整读取键盘的输入流，包括所有按键，然后发送给shell
    // 默认参数是shell,v2,pty:，这样也会读取完整的键盘输入流，但是格式经过一定编码，例如按下字母a会首先发送00 01 00 00 00 61，用ShellProtocol编码
    // 然后会发送不清楚含义的01 00 00 ... 和 00 00 ....，可能和终端的输入流有关系 前面的1看起来像是ack一样的东西，每个命令后都有，可能不是adbclient发的
    // 如果使用-T，也就是shell,v2,raw: ，不会给定完整的键盘输入流，只有在回车提交的时候，会按照上面的格式发送，例如输入a然后回车，就是00 02 00 00 00 61 0a，长度变为2，添加了\n，这种比较适合代码里传输命令
    // 这种在终端上看来是没有任何多余输出的
    // 并且似乎不会出现shell,raw:这种组合
    if (use_shell_protocol) shell += ",v2"; // 使用v2版本，将返回结果返回为probuf，没有额外内容 可能v2适合单条，这里用了v2就不返回数据了。。不过看源码的交互终端中也能按照一定规则启用v2，还需要研究，应该还是命令有问题
    shell += ":";

    SOCKET m_sock(adb_connect(nullptr, shell, error)); // 资源管理很混乱。。。
    if (m_sock < 0)
    {
        return -1;
    }
    // 防止阻塞，在最后加一个exit，虽然一组命令中间也可能存在exit使其不阻塞，但一般这样不会是用户的预期行为
    if (cmds.at(cmds.size() - 1) != "exit" || cmds.at(cmds.size() - 1) != "exit\n") cmds.push_back("exit");
    // 这里adb源代码中执行了waid-for-device，host-serial:712KPKN1261909:wait-for-any-device，还不清楚作用，可能需要根据源代码解析添加
    //std::string shell_device;
    //if (!use_shell_protocol) read_and_dump(m_sock, use_shell_protocol, shell_device); // 不适用协议发送会返回协议前缀和输入的命令的等东西
    std::string tmp;
    // 异步读取，会在我关闭发送的时候返回
    std::future<int> codef = std::async(std::launch::async, &read_and_dump, m_sock, use_shell_protocol, std::ref(tmp));
    for (auto cmd : cmds)
    {
        if(!cmd.ends_with("\n")) cmd += '\n'; // 需要换行符提交命令

        if (use_shell_protocol)
        {
            std::unique_ptr<ShellProtocol> protocol = std::make_unique<ShellProtocol>(m_sock);
            memcpy(protocol->data(), cmd.c_str(), cmd.size());
            if (!protocol->Write(ShellProtocol::kIdStdin, cmd.size()))
            {
                *error = "Write command error";
                closesocket(m_sock);
                return -1;
            }
        }else if (!WriteFdExactly(m_sock, cmd.c_str(), cmd.size()))
        {
            *error = "Write command error";
            closesocket(m_sock);
            return -1;
        }
        //std::string tmp;
        //if (int code = read_and_dump(m_sock, use_shell_protocol, tmp) != 0) { // 命令本身出错
        //    error = &tmp;
        //    closesocket(m_sock);
        //    return code;
        //}
        //else
        //{
        //    if (use_shell_protocol) *result += tmp; // 用了v2直接在结尾添加输出
        //    else {
        //        // 会在开头返回输入的命令，结尾返回前缀，需要去掉
        //        if (int p1 = tmp.find_first_of(cmd) != std::string::npos) tmp.substr(p1);
        //        if (int p2 = tmp.find_last_of(shell_device.c_str()) != std::string::npos) tmp.substr(0, p2);
        //        *result += tmp;
        //    }
        //}
    }

    
    //std::string tmp;
    int code = codef.get();
    if (code != 0) { // 命令本身出错
        error = &tmp;
        closesocket(m_sock);
        return code;
    }

    std::cout << tmp;

    closesocket(m_sock);
    return use_shell_protocol ? 0 : -2;
}

bool AdbClient::adb_query(const std::string& service, std::string* result, std::string* error, bool force_switch_device) {
    return BaseClient::adb_query(service, m_serial, m_transport_id, result,error, force_switch_device);
}

std::string AdbClient::adb_query(const std::string& service, bool force_switch_device)
{
    return  BaseClient::adb_query(service, m_serial, m_transport_id, force_switch_device);
}

bool AdbClient::adb_command(const std::string& service) {
    return BaseClient::adb_command(service, m_serial, m_transport_id);
}

int AdbClient::adb_connect(uint64_t* transport, std::string service, std::string* error, bool force_switch_device) {
    return BaseClient::adb_connect(transport, service, m_serial, m_transport_id, error, force_switch_device);
}

int AdbClient::adb_connect(std::string service, std::string* error) {
    BaseClient::adb_connect(service, m_serial, m_transport_id, error);
}


// 重启
bool AdbClient::reboot(int mod, std::string* error)
{
    switch (mod)
    {
    case 1: 
        return adb_connect("reboot:", error);
    case 2:
        return adb_connect("reboot:bootloader", error);
    case 3:
        return adb_connect("reboot:recovery", error);
    case 4:
        return adb_connect("reboot:sideload", error);
    case 5:
        return adb_connect("reboot:sideload-auto-reboot", error);
    case 6:
        return adb_connect("shell:stop", error) && adb_connect("shell:start", error); // 软重启，重启Zygote
    default:
        return adb_connect("reboot:", error);
    }
}

const std::string AdbClient::ADB_HOST = "127.0.0.1";