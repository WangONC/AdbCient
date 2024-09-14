#include "AdbClient.h"
#include <fstream>
#include <iostream>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <tlhelp32.h>
#include "ShellProtocol.h"
#pragma comment(lib, "ws2_32.lib")

#define CHECK_NE(a, b) \
    if ((a) == (b)) abort();

// http://aospxref.com/android-13.0.0_r3/xref/system/libbase/stringprintf.cpp
// ����ת������д���ˣ�ֱ���������ˣ�����Ӧ�ö�������������
void StringAppendV(std::string* dst, const char* format, va_list ap) {
    // First try with a small fixed size buffer
    char* space = new char[1024];

    // It's possible for methods that use a va_list to invalidate
    // the data in it upon use.  The fix is to make a copy
    // of the structure before using it and use that copy instead.
    va_list backup_ap;
    va_copy(backup_ap, ap);
    int result = vsnprintf(space, sizeof(space), format, backup_ap);
    va_end(backup_ap);

    if (result < static_cast<int>(sizeof(space))) {
        if (result >= 0) {
            // Normal case -- everything fit.
            dst->append(space, result);
            return;
        }

        if (result < 0) {
            // Just an error.
            return;
        }
    }

    // Increase the buffer size to the size requested by vsnprintf,
    // plus one for the closing \0.
    int length = result + 1;
    char* buf = new char[length];

    // Restore the va_list before we use it again
    va_copy(backup_ap, ap);
    result = vsnprintf(buf, length, format, backup_ap);
    va_end(backup_ap);

    if (result >= 0 && result < length) {
        // It fit
        dst->append(buf, result);
    }
    delete[] buf;
}

std::string StringPrintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string result;
    StringAppendV(&result, fmt, ap);
    va_end(ap);
    return result;
}

// http://aospxref.com/android-13.0.0_r3/xref/system/libbase/strings.cpp#37
std::vector<std::string> Split(const std::string& s,
    const std::string& delimiters) {
    CHECK_NE(delimiters.size(), 0U);

    std::vector<std::string> result;

    size_t base = 0;
    size_t found;
    while (true) {
        found = s.find_first_of(delimiters, base);
        result.push_back(s.substr(base, found - base));
        if (found == s.npos) break;
        base = found + 1;

    }

    return result;

}


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

AdbClient::AdbClient()
{
    m_serial = "";
    m_transport_id = nullptr;
    if (!initializeWsa()) {
        throw std::runtime_error("WSAStartup failed");
    }
}

AdbClient::AdbClient(std::string serial)
{
    m_serial = serial;
    m_transport_id = nullptr;
    if (!initializeWsa()) {
        throw std::runtime_error("WSAStartup failed");
    }
}

AdbClient::AdbClient(uint64_t* transport_id)
{
    m_serial = "";
    m_transport_id = transport_id;
    if (!initializeWsa()) {
        throw std::runtime_error("WSAStartup failed");
    }
}

AdbClient::AdbClient(std::string serial, uint64_t* transport_id)
{
    m_serial = serial;
    m_transport_id = transport_id;
    if (!initializeWsa()) {
        throw std::runtime_error("WSAStartup failed");
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

// ����������adb server������ʧ���򲻻��ʼ���ڲ����Բ�����false
bool AdbClient::createAndConnectSocket(SOCKET &m_sock) {
    m_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_sock == INVALID_SOCKET) {
        std::cerr << "Socket creation error" << std::endl;
        return false;
    }

    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(ADB_PORT);
    inet_pton(AF_INET, ADB_HOST.c_str(), &serv_addr.sin_addr);

    if (connect(m_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        std::cerr << "Connection Failed" << std::endl;
        closesocket(m_sock);
        return false;
    }
    //bool q = is_socket_ready(m_sock, 5,false);
    //m_sock = sock;

    // �趨����
    int interval_sec = 1; // adb��Ĭ���趨 http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/socket_spec.cpp?fi=socket_spec_connect#235
    tcp_keepalive keepalive;
    keepalive.onoff = (interval_sec > 0); // �ֶα�ʾ�Ƿ����� keepalive
    keepalive.keepalivetime = interval_sec * 1000; // �ڷ��͵�һ�� keepalive ̽���֮ǰ�ȴ���ʱ�䣨�Ժ���Ϊ��λ����
    keepalive.keepaliveinterval = interval_sec * 1000; // ���ͺ��� keepalive ̽���֮���ʱ�������Ժ���Ϊ��λ����

    DWORD bytes_returned = 0;
    if (WSAIoctl(m_sock, SIO_KEEPALIVE_VALS, &keepalive, sizeof(keepalive), nullptr, 0,
        &bytes_returned, nullptr, nullptr) != 0) {
        const DWORD err = WSAGetLastError();
        return false;
    }

    // ����nagle http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/sysdeps.h?fi=disable_tcp_nagle#782
    int off = 1;
    setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&off), sizeof(off));

    return true;
}

// ����adb server���ص�״̬
bool AdbClient::adb_status(SOCKET& m_sock,std::string* error)
{
        char buf[5];
        if (!ReadFdExactly(m_sock, buf, 4)) {
            *error = std::string("protocol fault (couldn't read status)");
            return false;
        }

        if (!memcmp(buf, "OKAY", 4)) {
            return true;
        }

        if (memcmp(buf, "FAIL", 4)) {
            char cerror[39];
            *error = std::snprintf(cerror,sizeof(cerror),"protocol fault (status %02x %02x %02x %02x?!)",
                buf[0], buf[1], buf[2], buf[3]);
            return false;
        }

        ReadProtocolString(m_sock, error, error);
        return false;
    
}

// �л��豸
std::optional<uint64_t> AdbClient::switch_socket_transport(SOCKET& m_sock, std::string* error)
{
    uint64_t result;
    std::string service;
    bool read_transport = true;
    if (m_transport_id) {
        read_transport = false;
        service += "host:transport-id:";
        std::string tmp = StringPrintf("%d", *m_transport_id);
        service += tmp;
        result = *m_transport_id;
    }
    else if (!m_serial.empty()) {
        service += "host:tport:serial:";
        service += m_serial;
    }
    // ������ʣ�µ�transport�������һ���������һ�㶼�ǵ��豸Ҳ�����л������ǲ����׻�ȡ�����ӷ�ʽ

    if (!SendProtocolString(m_sock, service, error)) {
         *error = "write failure during connection";
         return std::nullopt;
    }
    if (!adb_status(m_sock,error))
        return std::nullopt;
    if (read_transport)
    {
        if (!ReadFdExactly(m_sock, &result, sizeof(result))) {
            *error = "failed to read transport id from server";
            return std::nullopt;
        }
    }

    return result;
}

// ������������رղ���ȡ
bool AdbClient::SendProtocolString(SOCKET& m_sock, const std::string& s, std::string* error=nullptr)
{
    unsigned int length = s.size();
    if (length > MAX_PAYLOAD - 4) {
        if (error != nullptr) *error = std::string("Message too long");
        errno = EMSGSIZE;
        return false;
    }

    char lengthStr[5];
    std::snprintf(lengthStr, sizeof(lengthStr), "%04x", static_cast<int>(s.size()));
    std::string msg = std::string(lengthStr) + s;

    return WriteFdExactly(m_sock, msg.c_str(),strlen(msg.c_str()));

    /*int result = send(m_sock, msg.c_str(), msg.length(), 0);
    if (result == SOCKET_ERROR) {
        if(error != nullptr) *error = "send failed: " + WSAGetLastError();
        return false;
    }

    return true;*/
}


// ֱ�Ӷ�ȡ���ݣ������Ǳ��ĸ�ʽ
bool AdbClient::ReadFdExactly(SOCKET fd, void* buf, size_t len)
{
    char* p = reinterpret_cast<char*>(buf);
    size_t len0 = len;
    while (len > 0) {
        int r = recv(fd, p, len,0);
        if (r > 0) {
            len -= r;
            p += r;
        } else if (r == -1) {
            return false; 
        }
        else {
            errno = 0;
            return false;
        }
    }
    return true;
}

// ֱ��д���ݣ������Ǳ��ĸ�ʽ
bool AdbClient::WriteFdExactly(SOCKET fd, const void* buf, size_t len)
{
    const char* p = reinterpret_cast<const char*>(buf);
    int r;

    while (len > 0) {
        r = send(fd, p, len, 0);
        if (r == -1) {
            if (errno == EAGAIN) {
                std::this_thread::yield();
                continue;
            }
            else if (errno == EPIPE) {
                errno = 0;
                return false;
            }
            else {
                return false;
            }
        }
        else {
            len -= r;
            p += r;
        }
    }
    return true;
}

bool AdbClient::adb_push(std::vector<std::string> local_path, std::string remote_path, std::string &error)
{
    // ��ʵ�ֻ������������߰����ѡ�ʱ���ټ�

    if (local_path.empty() || remote_path.empty()) {
        error = "push requires an argument";
        return false;
    }

    return false;
}

// ���Ķ�ȡ
bool AdbClient::ReadProtocolString(SOCKET& m_sock, std::string* s, std::string* error)
{
    char buf[5];
    if (!ReadFdExactly(m_sock, buf, 4)) {
        *error = std::string("protocol fault (couldn't read status length)");
        return false;
    }
    buf[4] = 0;

    unsigned long len = strtoul(buf, nullptr, 16);
    s->resize(len, '\0');
    if (!ReadFdExactly(m_sock, &(*s)[0], len)) {
        *error = std::string("protocol fault (couldn't read status message)");
        return false;
    }

    return true;
}

// ��������ĵײ㺯��
int AdbClient::_adb_connect(std::string service, uint64_t* transport, std::string* error, bool force_switch)
{
    if (service.empty() || service.size() > MAX_PAYLOAD) {
        *error = StringPrintf("bad service name length (%zd)", service.size());
        return -1;
    }

    std::string reason;
    SOCKET m_sock; // Դ������ʵ���������õ�socket���Լ������ģ����ǹ���
    if (!createAndConnectSocket(m_sock) || m_sock == INVALID_SOCKET) {
        *error = StringPrintf("cannot connect to daemon at %s: %s",
            m_sock, reason.c_str());
        return -2;
    }

    if (!service.starts_with("host") || force_switch) {
        std::optional<uint64_t> transport_result = switch_socket_transport(m_sock,error);
        if (!transport_result) {
            return -1;
        }

        if (transport) {
            *transport = *transport_result;
        }
    }

    if (!SendProtocolString(m_sock,service, error)) {
        closesocket(m_sock); // ��ȻԴ��������û�йرգ���������Ϊ�˼�������ָ�룬���ⲻ�ؾͳ�Ұָ����...
        return -1;
    }

    if (!adb_status(m_sock,error)) {
        closesocket(m_sock);
        return -1;
    }

    
    return m_sock;
}

int AdbClient::adb_connect(uint64_t* transport, std::string service, std::string* error, bool force_switch_device)
{
    // if the command is start-server, we are done.
    if (strcmp(service.c_str(), "host:start-server") == 0) {
        return 0;
    }

    int fd(_adb_connect(service, transport, error, force_switch_device));
    if (fd == -1) {
        //D("_adb_connect error: %s", error->c_str());
    }
    else if (fd == -2) {
        fprintf(stderr, "* daemon still not running\n");
    }
    return fd;
}

int AdbClient::read_and_dump(SOCKET fd,bool use_shell_protocol,std::string &result)
{
    int exit_code = 0;
    if (fd < 0) return exit_code;

    if (use_shell_protocol) {
        std::string raw_out_buffer;
        size_t out_len;
        std::string raw_err_buffer;
        size_t err_len;
        exit_code = read_and_dump_protocol(fd, raw_out_buffer,out_len,raw_err_buffer,err_len);
        if (exit_code == 0) result += raw_out_buffer;
        else result += raw_err_buffer;
    }
    else {
        char raw_buffer[BUFSIZ];
        char* buffer_ptr = raw_buffer;
        while (true) {
            int length = recv(fd, raw_buffer, sizeof(raw_buffer) - 1, 0); // ���ʣ��һ���ֽ����ڴ����ֹ��
            if (length <= 0) {
                break;
            }
            /*if (!callback->OnStdout(buffer_ptr, length)) {
                break;
            }*/

            raw_buffer[length] = '\0';  // ֻ�е����յ����ֽ������ڻ�������Сʱ������� '\0'
            result += raw_buffer;

        }
    }

    return exit_code;
}

int AdbClient::read_and_dump_protocol(SOCKET fd, std::string& out, size_t& out_size, std::string& err, size_t& err_size)
{
    out_size = 0;
    err_size = 0;
    int exit_code = 0;
    char raw_buffer[BUFSIZ];

    std::unique_ptr<ShellProtocol> protocol = std::make_unique<ShellProtocol>(fd);
    if (!protocol) {
        return 1;
    }
    while (protocol->Read()) {
        size_t data_len = protocol->data_length();
        if (protocol->id() == ShellProtocol::kIdStdout) {
                std::memset(raw_buffer, 0, BUFSIZ);
                std::memcpy(raw_buffer, protocol->data(), data_len);
                out_size += data_len;
                out += raw_buffer;

            //if (!callback->OnStdout(protocol->data(), protocol->data_length())) {
            //    exit_code = SIGPIPE + 128;
            //    break;
            //}
        }
        else if (protocol->id() == ShellProtocol::kIdStderr) {
                std::memset(raw_buffer, 0, BUFSIZ);
                std::memcpy(raw_buffer, protocol->data(), data_len);
                err_size += data_len;
                err += raw_buffer;
                /*if (!callback->OnStderr(protocol->data(), protocol->data_length())) {
                exit_code = SIGPIPE + 128;
                break;
            }*/
        }
        else if (protocol->id() == ShellProtocol::kIdExit) {
            // data() returns a char* which doesn't have defined signedness.
            // Cast to uint8_t to prevent 255 from being sign extended to INT_MIN,
            // which doesn't get truncated on Windows.
            exit_code = static_cast<uint8_t>(protocol->data()[0]);
        }
    }

    return exit_code;
}

std::vector<std::string> AdbClient::adb_get_feature(std::string* error)
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
    else if(!m_serial.empty()) prefix = StringPrintf("host-serial:%s:", m_serial.c_str());
    else
    {
        *error = "";
        return {};
    }
    if (!adb_query(prefix + "features", &result, error)) return {};
    return Split(result, ",");
}

// ���ĳ�������Ƿ������У�������Էŵ�ʲô����������
bool AdbClient::isProcessRunning(const WCHAR* processName) {
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
    bool r = adb_status(fd ,&error);
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
            dInfo.deviceState = toConnectionState((std::string)status.c_str());

            device_infostr = device.substr(status_pos + 1); // �ضϺ����������Ϣ

            // ��ʼ������Ϣ��˳���ǹ̶��ģ���������ʲô���Ҳ���Բ�ѭ��ֱ�Ӱ�˳���
            size_t startinfo = 0;
            size_t space = device_infostr.find(" ");
            while (space != std::string::npos)
            {
                std::string info = device_infostr.substr(startinfo, space - startinfo);
                if (info.find(":") == std::string::npos) { // û��:����devpath,��һ�㶼û�������Ŀ append_transport_info(result, "", t->devpath, false);
                    dInfo.deviceState = toConnectionState((std::string)info.c_str());
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

// ����ִ��û�н��������ƺ�ֻ��wait-for��
bool AdbClient::adb_command(const std::string& service)
{
     std::string error;
     SOCKET fd(adb_connect(service, &error));
     if (fd < 0) {
         fprintf(stderr, "error: %s\n", error.c_str());
         return false;
     }

     if (!adb_status(fd, &error)) {
         fprintf(stderr, "error: %s\n", error.c_str());
         return false;
     }
     closesocket(fd);
     return true;
}

// ����ִ����Ҫ����������host:devices 
bool AdbClient::adb_query(const std::string& service, std::string* result, std::string* error, bool force_switch_device)
{
    SOCKET fd(adb_connect(nullptr, service, error, force_switch_device));
    if (fd < 0) {
        closesocket(fd);
        return false;
    }

    result->clear();
    if (!ReadProtocolString(fd, result, error)) {
        closesocket(fd);
        return false;
    }
    closesocket(fd);
    return true;
}

// �򵥴ֱ��ĺ���,����������ؽ���������ı�����������û�н�������
std::string AdbClient::adb_query(const std::string& service, bool force_switch_device)
{
    std::string result, error;
    adb_query(service, &result, &error, force_switch_device);
    return std::string();
}

// ��adb_command�����С���ƺ�ֻ�йز����Ĵ�����������õĶ�,�����������ر�socket
int AdbClient::adb_connect(std::string service, std::string* error)
{
    return adb_connect(nullptr, service, error);
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
    auto features = adb_get_feature(error);
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
    auto features = adb_get_feature(error);
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
    std::future<int> codef = std::async(std::launch::async, &AdbClient::read_and_dump,this, m_sock, use_shell_protocol, std::ref(tmp));
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

std::string to_string(ConnectionState state) {
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

ConnectionState toConnectionState(const std::string& state) {
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