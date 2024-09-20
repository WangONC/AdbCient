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
    if (di.size() == 1)// û���ֶ�ָ���豸������£�ֻ��һ���豸�Ż��Զ���ȡ��Ϊ�˱����Զ���ȡĳ���豸��ɻ�������������޶�ģ��ԭ��adb��Ϊ
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

// �ն�����ִ�еķ�ʽ��powershell���첽ִ��adb����
std::future<CommandExecution::CommandResult> AdbClient::run_async(const std::string& command)
{
    return CommandExecution::ExecCommandAsync("adb.exe " + command);
}

// �ն�����ִ�еķ�ʽ��powershell��������ִ��adb����
std::string AdbClient::run(const std::string& command,bool& out_isSuccess)
{
    // ʹ�� CommandExecution ���ͬ��ִ�������
    auto result = CommandExecution::ExecCommand("adb.exe " + command);
    out_isSuccess = result.result == 0;
    if (!out_isSuccess) {
        return result.errorMsg;
    }
    return result.successMsg;
}

// �ն�����ִ�еķ�ʽ��powershell��������ִ��adb����
std::string AdbClient::run(const std::string& command)
{
    // ʹ�� CommandExecution ���ͬ��ִ�������
    auto result = CommandExecution::ExecCommand("adb.exe " + command);
    if (result.result != 0) {
        return result.errorMsg;
    }
    return result.successMsg;
}

// ͨ��ִ���ն�����ķ�ʽ��ȡ���ӵ��豸������
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

// ͨ���ն�����ִ���첽��װ���apk�����������������Ƿ�ɹ�
bool AdbClient::InstallAPK(const std::string& deviceId, std::vector<std::string> apks, bool force)
{
    if (deviceId.empty()) {
        return false;
    }

    std::ostringstream commandStream;
    commandStream << "-s " << deviceId << " install-multi-package";
    if (force) commandStream << " -r -d -t"; // ǿ�ư�װ�������ǡ�����������ǩ

    
    bool hasValidApk = false;
    for (auto apk : apks)
    {
        std::ifstream file(apk);
        if (!file.good()) continue;
        // adbֻ֧��.apk��.apex
        std::string lowerApk = apk;
        std::transform(lowerApk.begin(), lowerApk.end(), lowerApk.begin(), ::tolower);
        if (lowerApk.substr(lowerApk.length() - 4) != ".apk" && lowerApk.substr(lowerApk.length() - 5) != ".apex") continue;
        commandStream << " \"" << apk << "\"";
        hasValidApk = true;
    }
    // ȷ��������һ����Ч�� APK �ļ��������ų���ѡ�����ļ�������apk��������apk�����
    if (!hasValidApk)  return false;

    // �첽ִ������
    run_async(commandStream.str());

    return true;
}

// ��ʼ�� Winsock 
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

// ���� Winsock 
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





// ���adb server�Ƿ������еķ�װ�ӿ�
bool AdbClient::isAdbServerRunning() {
    return isProcessRunning(_T("adb.exe"));
}
 
// ��������adb server��ʵ������ֱ��ִ��adb.exe start-server
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
    if (!error.empty()) return false; //��ʱ����д�����Ϣ˵��ִ��ʧ����
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

    // ����������Ӧ�ַ���
    size_t start = 0;
    size_t end = data.find("\n");

    // ����ÿһ��
    while (end != std::string::npos) {
        std::string device = data.substr(start, end - start);
        size_t tab_pos = device.find("\t");

        // ����Ƿ��ҵ��豸�������豸״̬�Ƿ�Ϊ "device"
        if (tab_pos != std::string::npos) {
            std::string device_serial = device.substr(0, tab_pos);
            std::string device_status = device.substr(tab_pos + 1);

            // ����豸״̬�Ƿ�Ϊ��Ч״̬���� "device"��
            if (device_status.find("device") != std::string::npos) {
                devices.push_back(device_serial);
            }
        }

        // �ƶ�����һ��
        start = end + 1;
        end = data.find("\n", start);
    }
    // ���һ��Ҳ��\n
    //// ������һ�У���ֹ��©���һ��û�л��з��������
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
        
        size_t tab_pos = device.find_first_of(" "); // devices -l�ĸ�ʽ����%-22s %s"

        if (tab_pos != std::string::npos) {
            std::string device_serial = device.substr(0, tab_pos);
            dInfo.serial = (std::string)device_serial.c_str();
            std::string device_infostr;
            int r_start_pos;
            if(tab_pos >= 22 ) r_start_pos = tab_pos + 1; // ���ݶ��붨��
            else r_start_pos = 22 + 1;


            size_t status_pos = device.find_first_of(" ", r_start_pos);

            std::string status = device.substr(r_start_pos, status_pos - r_start_pos); // ���кŽ�������״̬
            dInfo.deviceState = DeviceInfo::toConnectionState((std::string)status.c_str());

            device_infostr = device.substr(status_pos + 1); // �ضϺ����������Ϣ

            // ��ʼ������Ϣ��˳���ǹ̶��ģ���������ʲô���Ҳ���Բ�ѭ��ֱ�Ӱ�˳���
            size_t startinfo = 0;
            size_t space = device_infostr.find(" ");
            while (space != std::string::npos)
            {
                std::string info = device_infostr.substr(startinfo, space - startinfo);
                if (info.find(":") == std::string::npos) { // û��:����devpath,��һ�㶼û�������Ŀ append_transport_info(result, "", t->devpath, false);
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


// �����ȡ�������벢��ϳ�һ���ķ�ʽ����������֧�ּ�������ĸ���ָ�����Ҳ֧�����������
// ����ִ��ʱ�����һ�������������ʺ��������г���
int AdbClient::adb_shell(std::vector<std::string> cmds, std::string* result, std::string* error)
{
    if (cmds.empty()) {
        if (error) *error = "No commands provided";
        return -1;
    }
    // ��������,base64
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

    // �������
    std::ostringstream combined_cmd;
    combined_cmd << "shell";


    bool use_shell_protocol = false; // �Ƿ�ʹ��v2�汾
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

    // ִ������
    SOCKET m_sock(adb_connect(nullptr, combined_cmd.str(), error));
    if (m_sock < 0) {
        return -1;
    }


    int ecode = read_and_dump(m_sock, use_shell_protocol, *result);
    // ��ʱ��sock�����Ѿ�������

    closesocket(m_sock);
    return use_shell_protocol ? ecode: -2;
}

// ����ʽ�ն�����ִ�����������Ӧʱ�䡣���֧��v2����Զ�ʹ��v2
// ��������-2����ζ��û��ʹ��v2����ζ�ŷ����벻�������ж�ִ�н����Ҳ�����д�����Ϣ��-1��ζ���ڲ�����ִ�й����г����˴���
// �ڲ��������������ʱ�������£��ȴ��ִ�п�ܶࣨ80%���ϣ�������Ҫ�������������ն˵�����ִ��
int AdbClient::adb_remote_shell(std::vector<std::string> cmds, std::string* result, std::string* error)
{
    if (cmds.empty()) {
        if (error) *error = "No commands provided";
        return -1;
    }

    std::string shell = "shell,raw";
    bool use_shell_protocol = false; // �Ƿ�ʹ��v2�汾
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
    // �������ʹ��-x������Ĭ�ϵ�shell:�����ն��л�������ȡ���̵����������������а�����Ȼ���͸�shell
    // Ĭ�ϲ�����shell,v2,pty:������Ҳ���ȡ�����ļ��������������Ǹ�ʽ����һ�����룬���簴����ĸa�����ȷ���00 01 00 00 00 61����ShellProtocol����
    // Ȼ��ᷢ�Ͳ���������01 00 00 ... �� 00 00 ....�����ܺ��ն˵��������й�ϵ ǰ���1����������ackһ���Ķ�����ÿ��������У����ܲ���adbclient����
    // ���ʹ��-T��Ҳ����shell,v2,raw: ��������������ļ�����������ֻ���ڻس��ύ��ʱ�򣬻ᰴ������ĸ�ʽ���ͣ���������aȻ��س�������00 02 00 00 00 61 0a�����ȱ�Ϊ2�������\n�����ֱȽ��ʺϴ����ﴫ������
    // �������ն��Ͽ�����û���κζ��������
    // �����ƺ��������shell,raw:�������
    if (use_shell_protocol) shell += ",v2"; // ʹ��v2�汾�������ؽ������Ϊprobuf��û�ж������� ����v2�ʺϵ�������������v2�Ͳ����������ˡ���������Դ��Ľ����ն���Ҳ�ܰ���һ����������v2������Ҫ�о���Ӧ�û�������������
    shell += ":";

    SOCKET m_sock(adb_connect(nullptr, shell, error)); // ��Դ����ܻ��ҡ�����
    if (m_sock < 0)
    {
        return -1;
    }
    // ��ֹ������������һ��exit����Ȼһ�������м�Ҳ���ܴ���exitʹ�䲻��������һ�������������û���Ԥ����Ϊ
    if (cmds.at(cmds.size() - 1) != "exit" || cmds.at(cmds.size() - 1) != "exit\n") cmds.push_back("exit");
    // ����adbԴ������ִ����waid-for-device��host-serial:712KPKN1261909:wait-for-any-device������������ã�������Ҫ����Դ����������
    //std::string shell_device;
    //if (!use_shell_protocol) read_and_dump(m_sock, use_shell_protocol, shell_device); // ������Э�鷢�ͻ᷵��Э��ǰ׺�����������ĵȶ���
    std::string tmp;
    // �첽��ȡ�������ҹرշ��͵�ʱ�򷵻�
    std::future<int> codef = std::async(std::launch::async, &read_and_dump, m_sock, use_shell_protocol, std::ref(tmp));
    for (auto cmd : cmds)
    {
        if(!cmd.ends_with("\n")) cmd += '\n'; // ��Ҫ���з��ύ����

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
        //if (int code = read_and_dump(m_sock, use_shell_protocol, tmp) != 0) { // ��������
        //    error = &tmp;
        //    closesocket(m_sock);
        //    return code;
        //}
        //else
        //{
        //    if (use_shell_protocol) *result += tmp; // ����v2ֱ���ڽ�β������
        //    else {
        //        // ���ڿ�ͷ��������������β����ǰ׺����Ҫȥ��
        //        if (int p1 = tmp.find_first_of(cmd) != std::string::npos) tmp.substr(p1);
        //        if (int p2 = tmp.find_last_of(shell_device.c_str()) != std::string::npos) tmp.substr(0, p2);
        //        *result += tmp;
        //    }
        //}
    }

    
    //std::string tmp;
    int code = codef.get();
    if (code != 0) { // ��������
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


// ����
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
        return adb_connect("shell:stop", error) && adb_connect("shell:start", error); // ������������Zygote
    default:
        return adb_connect("reboot:", error);
    }
}

const std::string AdbClient::ADB_HOST = "127.0.0.1";