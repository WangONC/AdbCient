#pragma once
#include <WinSock2.h>
#include <string>

bool ReadFdExactly(SOCKET fd, void* buf, size_t len); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/adb_io.cpp?fi=SendProtocolString#76
bool WriteFdExactly(SOCKET fd, const void* buf, size_t len); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/adb_io.cpp?fi=SendProtocolString#103
int read_and_dump(SOCKET fd, bool use_shell_protocol, std::string& result); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/commandline.cpp#317
int read_and_dump_protocol(SOCKET fd, std::string& out, size_t& out_size, std::string& err, size_t& err_size); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/client/commandline.cpp#read_and_dump_protocol
bool SendProtocolString(SOCKET& m_sock, const std::string& s, std::string* error); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/adb_io.cpp?fi=SendProtocolString#37
bool ReadProtocolString(SOCKET& m_sock, std::string* s, std::string* error); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/adb_io.cpp?fi=SendProtocolString#50
bool ReadOrderlyShutdown(SOCKET fd, int direction); // http://aospxref.com/android-13.0.0_r3/xref/packages/modules/adb/adb_io.cpp?fi=ReadOrderlyShutdown#151