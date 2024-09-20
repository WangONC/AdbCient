#pragma once
#include <string>
#include <vector>
#include <sstream>

#include <Windows.h>

#define CHECK_NE(a, b) \
    if ((a) == (b)) abort();


// http://aospxref.com/android-13.0.0_r3/xref/system/libbase/stringprintf.cpp
// 懒得转成其他写法了，直接贴过来了，或许应该丢进工具类里面
void StringAppendV(std::string * dst, const char* format, va_list ap);
std::string StringPrintf(const char* fmt, ...);
void StringAppendF(std::string * dst, const char* format, ...);

// http://aospxref.com/android-13.0.0_r3/xref/system/libbase/strings.cpp#37
std::vector<std::string> Split(const std::string & s,
    const std::string & delimiters);

std::wstring CharToWstring(const char* str);

std::string Dirname(const std::string & path);
std::string Basename(const std::string & path);
std::string WstringToString(const std::wstring & wstr);
std::string escape_arg(const std::string & s);

template <typename ContainerT, typename SeparatorT>
std::string Join(const ContainerT & things, SeparatorT separator) {
    if (things.empty()) {
        return "";
    }

    std::ostringstream result;
    result << *things.begin();
    for (auto it = std::next(things.begin()); it != things.end(); ++it) {
        result << separator << *it;
    }
    return result.str();
}

std::vector<std::string> Tokenize(const std::string& s, const std::string& delimiters);
std::string Trim(const std::string& s);
bool ReadFdToString(HANDLE fd, std::string* content);
bool ReadFileToString(const std::string& path, std::string* content, bool follow_symlinks, std::string* error);

void _socket_set_errno(const DWORD err);


