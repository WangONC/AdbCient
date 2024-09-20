#pragma once
#include <string>
#include <WinSock2.h>
#include <optional>
#include <chrono>

namespace BaseClient {
	static const size_t MAX_PAYLOAD = 1024 * 1024;
	static const int ADB_PORT = 5037; // adb的默认端口
	static const std::string ADB_HOST = "127.0.0.1"; // 默认的本机连接


	int _adb_connect(std::string service, std::string m_serial, uint64_t* m_transport_id, uint64_t* transport, std::string* error, bool force_switch); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/adb_client.cpp#158
	int adb_connect(uint64_t* transport, std::string service, std::string m_serial, uint64_t* m_transport_id, std::string* error, bool force_switch_device = false); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/adb_client.cpp#349
	bool adb_command(const std::string& service, std::string m_serial, uint64_t* m_transport_id); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/adb_client.cpp#374
	bool adb_query(const std::string& service, std::string m_serial, uint64_t* m_transport_id, std::string* result, std::string* error, bool force_switch_device = false); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/adb_client.cpp#391
	std::string adb_query(const std::string& service, std::string m_serial, uint64_t* m_transport_id, bool force_switch_device = false);
	int adb_connect(std::string service, std::string m_serial, uint64_t* m_transport_id, std::string* error); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/adb_client.cpp#238
	bool createAndConnectSocket(SOCKET& m_sock);
	bool adb_status(SOCKET& m_sock, std::string* error); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/adb_client.cpp#137
	std::optional<uint64_t> switch_socket_transport(SOCKET& m_sock, std::string m_serial, uint64_t* m_transport_id, std::string* error); // 适用于多设备 http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/adb_client.cpp#81
	int send_shell_command(std::string m_serial, uint64_t* m_transport_id, const std::string& command, bool disable_shell_protocol, std::string* result, std::string* error);
	bool wait_for_device(const char* service, std::string m_serial, uint64_t* m_transport_id, std::string* error, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
}

