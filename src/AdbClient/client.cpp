#include "AdbClient/client.h"
#include "AdbClient/adb_io.h"
#include "misc/StringUtils.h"
#include <iostream>
#include <mstcpip.h>
#include <ws2tcpip.h>
#include <AdbClient/AdbUtils.h>
#include <chrono>
#include <thread>
#include <mutex>


namespace BaseClient {
    // ��������ĵײ㺯��
    int _adb_connect(std::string service, std::string m_serial, uint64_t* m_transport_id, uint64_t* transport, std::string* error, bool force_switch)
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
            std::optional<uint64_t> transport_result = switch_socket_transport(m_sock, m_serial, m_transport_id, error);
            if (!transport_result) {
                return -1;
            }

            if (transport) {
                *transport = *transport_result;
            }
        }

        if (!SendProtocolString(m_sock, service, error)) {
            closesocket(m_sock); // ��ȻԴ��������û�йرգ���������Ϊ�˼�������ָ�룬���ⲻ�ؾͳ�Ұָ����...
            return -1;
        }

        if (!adb_status(m_sock, error)) {
            closesocket(m_sock);
            return -1;
        }


        return m_sock;
    }

    int adb_connect(uint64_t* transport, std::string service, std::string m_serial, uint64_t* m_transport_id, std::string* error, bool force_switch_device)
    {
        // if the command is start-server, we are done.
        if (strcmp(service.c_str(), "host:start-server") == 0) {
            return 0;
        }

        int fd(_adb_connect(service, m_serial, m_transport_id, transport, error, force_switch_device));
        if (fd == -1) {
            //D("_adb_connect error: %s", error->c_str());
        }
        else if (fd == -2) {
            fprintf(stderr, "* daemon still not running\n");
        }
        return fd;
    }

    // ����������adb server������ʧ���򲻻��ʼ���ڲ����Բ�����false
    bool createAndConnectSocket(SOCKET& m_sock) {
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
    bool adb_status(SOCKET& m_sock, std::string* error)
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
            *error = std::snprintf(cerror, sizeof(cerror), "protocol fault (status %02x %02x %02x %02x?!)",
                buf[0], buf[1], buf[2], buf[3]);
            return false;
        }

        ReadProtocolString(m_sock, error, error);
        return false;

    }

    // �л��豸��transport_id�����к�ָ��һ������
    std::optional<uint64_t> switch_socket_transport(SOCKET& m_sock, std::string m_serial, uint64_t* m_transport_id, std::string* error)
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
        if (!adb_status(m_sock, error))
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

    // ����������ε������ͣ���������������أ�adb�л�һֱ���Բ��ȴ��豸������
    int send_shell_command(std::string m_serial, uint64_t* m_transport_id, const std::string& command, bool disable_shell_protocol, std::string* result, std::string* error)
    {
        SOCKET fd;
        bool use_shell_protocol = false;



        // Use shell protocol if it's supported and the caller doesn't explicitly
        // disable it.
        if (!disable_shell_protocol) {
            auto features = adb_get_feature(m_serial, m_transport_id, error);
            if (!features.empty()) {
                use_shell_protocol = CanUseFeature(features, std::string(kFeatureShell2));
            }
            else return -3;
        }
        std::string service_string = ShellServiceString(use_shell_protocol, "", command);

        fd = adb_connect(service_string, m_serial, m_transport_id, error);
        if (fd < 0) return fd;
        return read_and_dump(fd, use_shell_protocol, *result);
    }
   



    // ����ִ��û�н��������ƺ�ֻ��wait-for��
    bool adb_command(const std::string& service, std::string m_serial, uint64_t* m_transport_id)
    {
        std::string error;
        SOCKET fd(adb_connect(service, m_serial, m_transport_id, &error));
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
    bool adb_query(const std::string& service, std::string m_serial, uint64_t* m_transport_id, std::string* result, std::string* error, bool force_switch_device)
    {
        SOCKET fd(adb_connect(nullptr, service, m_serial, m_transport_id, error, force_switch_device));
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
    std::string adb_query(const std::string& service, std::string m_serial, uint64_t* m_transport_id, bool force_switch_device)
    {
        std::string result, error;
        adb_query(service, m_serial, m_transport_id, &result, &error, force_switch_device);
        return std::string();
    }

    // ��adb_command�����С���ƺ�ֻ�йز����Ĵ�����������õĶ�,�����������ر�socket
    int adb_connect(std::string service, std::string m_serial, uint64_t* m_transport_id, std::string* error)
    {
        return adb_connect(nullptr, service, m_serial, m_transport_id, error);
    }

    // һ����˵�ڿ����ô����������Ӧ�ó���Ϊ�˿����ȶ���Ӧ���ȴ��豸��Ч��������Ϊ��ʹ���豸������Ӧ����������򷵻�false�����ɵ����ߴ���Ҳ�ܱ�������������ɵ��޷������豸���ֵ�����
    bool wait_for_device(const char* service, std::string m_serial, uint64_t* m_transport_id, std::string* error,std::optional<std::chrono::milliseconds> timeout) {
        std::vector<std::string> components = Split(service, "-");
        if (components.size() < 3) {
            *error = StringPrintf("adb: couldn't parse 'wait-for' command: %s", service);
            return false;
        }

        if (components[2] != "any") {
            auto it = components.begin() + 2;
            components.insert(it, "any");
        }

        // Stitch it back together and send it over...
        std::string cmd = format_host_command(m_serial, m_transport_id, Join(components, "-").c_str());

        std::mutex mtx;
        std::condition_variable cv;
        bool finished = false;
        bool success = false;
        std::thread worker([&]() {
            success = adb_command(cmd, m_serial, m_transport_id);
            {
                std::lock_guard<std::mutex> lock(mtx);
                finished = true;
            }
            cv.notify_one();
            });

        if (timeout) {
            std::unique_lock<std::mutex> lock(mtx);
            if (cv.wait_for(lock, *timeout, [&] { return finished; })) {
                // Command completed before timeout
                worker.join();
                if (!success) {
                    *error = "adb command failed";
                }
                return success;
            }
            else {
                // Timeout occurred
                *error = "timeout expired while waiting for device";
                return false;
            }
        }
        else {
            worker.join();
            if (!success) {
                *error = "adb command failed";
            }
            return success;
        }
    }
}
