#include "misc/StringUtils.h"
#include <cstdarg>
#include <iostream>
#include <string>
#include <locale>
#include <codecvt>
#include <mutex>
#include <direct.h>
#include <windows.h>
#include <sys/stat.h>
#include <sys/types.h>
// http://aospxref.com/android-13.0.0_r3/xref/system/libbase/stringprintf.cpp
// 懒得转成其他写法了，直接贴过来了，或许应该丢进工具类里面
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

void StringAppendF(std::string* dst, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    StringAppendV(dst, format, ap);
    va_end(ap);
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

std::wstring CharToWstring(const char* str) {
    if (str == nullptr) {
        return std::wstring();
    }

    // 使用 codecvt_utf8_utf16 进行字符编码转换
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(str);
}

// 获取目录名称
std::string Dirname(const std::string& path) {
    if (path.empty()) return ".";

    size_t pos = 0;
    size_t prefix_end = 0;
    size_t last_sep = std::string::npos;

    // Handle UNC path
    if (path.length() >= 2 && (path[0] == '/' || path[0] == '\\') && (path[1] == '/' || path[1] == '\\')) {
        pos = 2;
        int unc_comps = 0;
        while (pos < path.length() && unc_comps < 2) {
            if ((path[pos] == '/' || path[pos] == '\\') && (pos == 2 || (path[pos - 1] != '/' && path[pos - 1] != '\\'))) {
                unc_comps++;
            }
            pos++;
        }
        prefix_end = pos;
    }
    // Handle drive letter
    else if (path.length() >= 2 && std::isalpha(path[0]) && path[1] == ':') {
        prefix_end = 2;
    }

    // Find last separator
    for (size_t i = prefix_end; i < path.length(); ++i) {
        if ((path[i] == '/' || path[i] == '\\') && (i == prefix_end || (path[i - 1] != '/' && path[i - 1] != '\\'))) {
            last_sep = i;
        }
    }

    if (last_sep == std::string::npos) {
        return (prefix_end > 0) ? path.substr(0, prefix_end) : ".";
    }

    // Remove trailing separators
    while (last_sep > prefix_end && (path[last_sep - 1] == '/' || path[last_sep - 1] == '\\')) {
        last_sep--;
    }

    if (last_sep == prefix_end) {
        last_sep++;  // Keep one separator for root directory
    }

    return path.substr(0, last_sep);
}

// 找出路径的文件名

std::string Basename(const std::string& path) {
    if (path.empty()) return ".";

    size_t pos = 0;
    size_t prefix_end = 0;
    size_t last_sep = std::string::npos;

    // Handle UNC path
    if (path.length() >= 2 && (path[0] == '/' || path[0] == '\\') && (path[1] == '/' || path[1] == '\\')) {
        pos = 2;
        int unc_comps = 0;
        while (pos < path.length() && unc_comps < 2) {
            if ((path[pos] == '/' || path[pos] == '\\') && (pos == 2 || (path[pos - 1] != '/' && path[pos - 1] != '\\'))) {
                unc_comps++;
            }
            pos++;
        }
        prefix_end = pos;
    }
    // Handle drive letter
    else if (path.length() >= 2 && std::isalpha(path[0]) && path[1] == ':') {
        prefix_end = 2;
    }

    // Find last non-separator character
    size_t end = path.length();
    while (end > prefix_end && (path[end - 1] == '/' || path[end - 1] == '\\')) {
        end--;
    }

    // Find last separator before the end
    for (size_t i = prefix_end; i < end; ++i) {
        if (path[i] == '/' || path[i] == '\\') {
            last_sep = i;
        }
    }

    if (last_sep == std::string::npos || last_sep < prefix_end) {
        return (end > prefix_end) ? path.substr(prefix_end, end - prefix_end) :
            ((path[0] == '/' || path[0] == '\\') ? "\\" : ".");
    }

    return path.substr(last_sep + 1, end - last_sep - 1);
}

std::string WstringToString(const std::wstring& wstr) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.to_bytes(wstr);
}

std::string escape_arg(const std::string& s) {
    // Escape any ' in the string (before we single-quote the whole thing).
    // The correct way to do this for the shell is to replace ' with '\'' --- that is,
    // close the existing single-quoted string, escape a single single-quote, and start
    // a new single-quoted string. Like the C preprocessor, the shell will concatenate
    // these pieces into one string.

    std::string result;
    result.push_back('\'');

    size_t base = 0;
    while (true) {
        size_t found = s.find('\'', base);
        result.append(s, base, found - base);
        if (found == s.npos) break;
        result.append("'\\''");
        base = found + 1;
    }

    result.push_back('\'');
    return result;
}

std::vector<std::string> Tokenize(const std::string& s, const std::string& delimiters) {
    CHECK_NE(delimiters.size(), 0U);

    std::vector<std::string> result;
    size_t end = 0;

    while (true) {
        size_t base = s.find_first_not_of(delimiters, end);
        if (base == s.npos) {
            break;
        }
        end = s.find_first_of(delimiters, base);
        result.push_back(s.substr(base, end - base));
    }
    return result;
}

std::string Trim(const std::string& s) {
    std::string result;

    if (s.size() == 0) {
        return result;
    }

    size_t start_index = 0;
    size_t end_index = s.size() - 1;

    // Skip initial whitespace.
    while (start_index < s.size()) {
        if (!isspace(s[start_index])) {
            break;
        }
        start_index++;
    }

    // Skip terminating whitespace.
    while (end_index >= start_index) {
        if (!isspace(s[end_index])) {
            break;
        }
        end_index--;
    }

    // All spaces, no beef.
    if (end_index < start_index) {
        return "";
    }
    // Start_index is the first non-space, end_index is the last one.
    return s.substr(start_index, end_index - start_index + 1);
}