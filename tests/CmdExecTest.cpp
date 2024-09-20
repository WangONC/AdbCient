// CmdExecTest.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include "CommandExecution/CommandExecution.h"

#include <windows.h>
#include <setupapi.h>
#include <iostream>
#include <string>

#include "AdbClient/AdbClient.h"
#include <Misc/SystemUtils.h>


#pragma comment(lib, "setupapi.lib")


void PrintResult(const std::string& testName, const CommandExecution::CommandResult& result) {
    std::cout << "Test: " << testName << "\n";
    std::cout << "Result code: " << result.result << "\n";
    std::cout << "Success message: " << result.successMsg << "\n";
    for (size_t i = 0; i < result.errorMsg.size(); ++i) {
        if (result.errorMsg[i] != '\0') {
            std::cout << result.errorMsg[i];
        }
        else {
            std::cout << "\\0";  // 输出 '\0' 位置
        }
    }
    std::cout << "Error message: " << result.errorMsg << "\n";
    std::cout << "------------------------\n";
}

void TestSyncSingleCommand() {
    auto result = CommandExecution::ExecCommand("Write-Output 'Hello, PowerShell!'");
    PrintResult("Sync Single Command", result);
}



#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define ADB_PORT 5037
struct AdbData {
    std::string status; // 是否成功
    std::string data; // 解析出的数据
};
AdbData send_adb_command(const std::string& command) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return {"ERRO","WSAStartup failed"};
    }
    
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation error" << std::endl;
        WSACleanup();
        return { "ERRO","Socket creation error" };
    }

    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(ADB_PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        std::cerr << "Connection Failed" << std::endl;
        closesocket(sock);
        WSACleanup();
        return { "ERRO","Connection Failed" };
    }

    // 发送命令，命令格式为 "长度 + 命令"
    char lengthStr[5]; // 4 位十六进制字符串 + 1 位终止符
    std::snprintf(lengthStr, sizeof(lengthStr), "%04x", static_cast<int>(command.size()));
    std::string msg = std::string(lengthStr).append(command);
    send(sock, msg.c_str(), msg.length(), 0);

    // 接收4字节的响应 ("OKAY" or "FAIL")
    char response[5] = { 0 };
    if (recv(sock, response, 4, 0) <= 0) {
        std::cerr << "Failed to receive response" << std::endl;
        closesocket(sock);
        WSACleanup();
        return { "ERRO","Failed to receive response" };
    }

    // 处理响应
    std::string result,status;
    if (strncmp(response, "OKAY", 4) == 0) {
        // 成功响应，继续接收数据
        char lengthBuffer[5] = { 0 };
        if (recv(sock, lengthBuffer, 4, 0) <= 0) {
            std::cerr << "Failed to receive data length" << std::endl;
            closesocket(sock);
            WSACleanup();
            return { "ERRO","Failed to receive data length" };
        }

        // 解析数据长度
        int dataLength = std::stoi(std::string(lengthBuffer), nullptr, 16);

        // 根据数据长度接收数据
        std::string data(dataLength, '\0');
        int received = recv(sock, &data[0], dataLength, 0);
        if (received > 0) {
            status = "OKAY";
            result =  data;
        }
        else {
            result = "Failed to receive complete data";
        }

    }
    else if (strncmp(response, "FAIL", 4) == 0) {
        // 失败响应，接收错误信息
        char lengthBuffer[5] = { 0 };
        if (recv(sock, lengthBuffer, 4, 0) <= 0) {
            std::cerr << "Failed to receive error length" << std::endl;
            closesocket(sock);
            WSACleanup();
            return { "ERRO","Failed to receive error length" };
        }

        // 解析错误信息长度
        int errorLength = std::stoi(std::string(lengthBuffer), nullptr, 16);

        // 根据错误信息长度接收数据
        std::string errorMessage(errorLength, '\0');
        int received = recv(sock, &errorMessage[0], errorLength, 0);
        if (received > 0) {
            status = "FAIL";
            result =  errorMessage;
        }
        else {
            status = "ERRO";
            result = "Failed to receive complete error message";
        }
    }
    else {
        status = "ERRO";
        result = "Unexpected response: " + std::string(response, 4);
    }

    // 关闭 socket 并清理
    closesocket(sock);
    WSACleanup();

    return { status, result };
}

std::vector<std::string> parse_device_list(const std::string& response) {
    std::vector<std::string> devices;

    // 处理整个响应字符串
    size_t start = 0;
    size_t end = response.find("\n");

    // 遍历每一行
    while (end != std::string::npos) {
        std::string device = response.substr(start, end - start);
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
        end = response.find("\n", start);
    }

    // 检查最后一行（防止遗漏最后一行没有换行符的情况）
    std::string last_device = response.substr(start);
    size_t tab_pos = last_device.find("\t");
    if (tab_pos != std::string::npos) {
        std::string device_serial = last_device.substr(0, tab_pos);
        std::string device_status = last_device.substr(tab_pos + 1);
        if (device_status.find("device") != std::string::npos) {
            devices.push_back(device_serial);
        }
    }

    return devices;
}


template<typename Func>
void measure_execution_time(Func&& func) {
    // 记录开始时间
    auto start = std::chrono::high_resolution_clock::now();

    // 执行传入的函数
    func();

    // 记录结束时间
    auto end = std::chrono::high_resolution_clock::now();

    // 计算时间差，以毫秒为单位
    std::chrono::duration<double, std::milli> execution_time = end - start;

    // 输出运行时间
    std::cout << "Execution time: " << execution_time.count() << " ms" << std::endl;
}

int main() {
#if defined(_FILE_OFFSET_BITS) && (_FILE_OFFSET_BITS == 64)
#ifdef _USE_32BIT_TIME_T
#define wstat _wstat32i64
#else
#define wstat _wstat64
#endif
#else
#endif
    struct _stat buf;
    _wstat(L"D:\\Environment\\llvm-mingw\\bin\\你好.ps1", &buf);
    struct adb_stat abuf;
    adb_stat("D:\\Environment\\llvm-mingw\\bin\\你好.ps1", &abuf);

    TestSyncSingleCommand();

    measure_execution_time([]() {
        /*std::string response = send_adb_command("host:devices").data;
        std::vector<std::string> devices = parse_device_list(response);*/
        AdbClient adb;
        std::vector<std::string> devices = adb.getConnectedDevices();
        for (auto a : devices) std::cout << a << std::endl;

        });

    measure_execution_time([]() {
        /*std::string response = send_adb_command("host:devices").data;
        std::vector<std::string> devices = parse_device_list(response);*/
        std::vector<std::string> devices1 = AdbClient::DetectDevices();
        for (auto a : devices1)
            std::cout << a << std::endl;

        });

    measure_execution_time([]() { // 这个逻辑更复杂反倒最快？？？为啥啊

        AdbClient adb;
        std::vector<DeviceInfo> devices = adb.getConnectedDevicesInfo();
        for (auto a : devices) {
            if(a.deviceState == kCsDevice)
                std::cout << a.serial << std::endl;
        }

        });

    measure_execution_time([]() {
        // 在这里写你想要测量的代码
        AdbClient adb("712KPKN1261909");
        std::vector<std::string> cmds = { "echo 'Simple command'" };
        std::string result1, error1;
        adb.adb_shell(cmds, &result1, &error1);
        std::cout << result1;


        });

    measure_execution_time([]() {
        // 在这里写你想要测量的代码
        AdbClient adb("712KPKN1261909");
        std::vector<std::string> cmds = { "echo 'Simple command'" };
        std::string result1, error1;
        adb.adb_remote_shell(cmds, &result1, &error1);
        std::cout << result1;
        

        });

    std::string response = send_adb_command("host:devices").data;
    if (!response.empty()) {
        std::vector<std::string> devices = parse_device_list(response);
        std::cout << "ADB Device Serials:" << std::endl;
        for (const auto& device : devices) {
            std::cout << device << std::endl;
        }
    }
    else {
        std::cout << "No response from ADB server or no devices connected." << std::endl;
    }
    AdbClient adb("712KPKN1261909");
    //bool a = adb.isAdbServerRunning();
    //bool b = adb.startAdbServer();
    //adb.stopAdbServer();
    std::string result, error;
    adb.adb_push({ "/123" }, "/data/local/tmp/", error);
    adb.adb_query("shell,raw:cd /data/local/tmp");
    adb.adb_query("shell,raw:ls", &result, &error, true);
    std::vector<std::string> cmds = { "echo 'Simple command'",
        "ls -l /data/local/tmp",
        "{ export VAR=\'Hello\'; echo $VAR; }",
        "for i in {1..5}; do\necho \"Loop iteration $i\"\ndate\nsleep 1\ndone",
        "ps | grep zygote",
        "free -m | awk \'/Mem:/ {print $2}\'" };
    std::vector<std::string> cmds1 = { "echo 'Simple command'" };
    std::string result1, error1;
    adb.adb_shell(cmds1, &result1, &error1);
    std::cout << result1;
    std::string result2, error2;
    cmds.push_back("exit");
    adb.adb_remote_shell(cmds, &result1, &error1);
    std::cout << result2;
    return 0;
}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
