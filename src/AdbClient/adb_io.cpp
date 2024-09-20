#include "AdbClient/adb_io.h"
#include "AdbClient/ShellProtocol.h"
#include <memory>
#include <thread>
#include <misc/StringUtils.h>

// 直接读取数据，不考虑报文格式
bool ReadFdExactly(SOCKET fd, void* buf, size_t len)
{
    char* p = reinterpret_cast<char*>(buf);
    size_t len0 = len;
    while (len > 0) {
        int r = recv(fd, p, len, 0);
        if (r > 0) {
            len -= r;
            p += r;
        }
        else if (r == -1) {
            return false;
        }
        else {
            errno = 0;
            return false;
        }
    }
    return true;
}

// 直接写数据，不考虑报文格式
bool WriteFdExactly(SOCKET fd, const void* buf, size_t len)
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

int read_and_dump(SOCKET fd, bool use_shell_protocol, std::string& result)
{
    int exit_code = 0;
    if (fd < 0) return exit_code;

    if (use_shell_protocol) {
        std::string raw_out_buffer;
        size_t out_len;
        std::string raw_err_buffer;
        size_t err_len;
        exit_code = read_and_dump_protocol(fd, raw_out_buffer, out_len, raw_err_buffer, err_len);
        if (exit_code == 0) result += raw_out_buffer;
        else result += raw_err_buffer;
    }
    else {
        char raw_buffer[BUFSIZ];
        char* buffer_ptr = raw_buffer;
        while (true) {
            int length = recv(fd, raw_buffer, sizeof(raw_buffer) - 1, 0); // 最后剩下一个字节用于存放终止符
            if (length <= 0) {
                break;
            }
            /*if (!callback->OnStdout(buffer_ptr, length)) {
                break;
            }*/

            raw_buffer[length] = '\0';  // 只有当接收到的字节数少于缓冲区大小时，才添加 '\0'
            result += raw_buffer;

        }
    }

    return exit_code;
}

int read_and_dump_protocol(SOCKET fd, std::string& out, size_t& out_size, std::string& err, size_t& err_size)
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

// 仅发送命令，不关闭不读取
bool SendProtocolString(SOCKET& m_sock, const std::string& s, std::string* error = nullptr)
{
    unsigned int length = s.size();
    if (length > BaseClient::MAX_PAYLOAD - 4) {
        if (error != nullptr) *error = std::string("Message too long");
        errno = EMSGSIZE;
        return false;
    }

    char lengthStr[5];
    std::snprintf(lengthStr, sizeof(lengthStr), "%04x", static_cast<int>(s.size()));
    std::string msg = std::string(lengthStr) + s;

    return WriteFdExactly(m_sock, msg.c_str(), strlen(msg.c_str()));

    /*int result = send(m_sock, msg.c_str(), msg.length(), 0);
    if (result == SOCKET_ERROR) {
        if(error != nullptr) *error = "send failed: " + WSAGetLastError();
        return false;
    }

    return true;*/
}

// 报文读取
bool ReadProtocolString(SOCKET& m_sock, std::string* s, std::string* error)
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

// 只有确认服务端关闭了之后才可以调用这个，否则会一直阻塞
bool ReadOrderlyShutdown(SOCKET fd, int direction) {
    char buf[16];
    int result = recv(fd, buf, sizeof(buf), 0);
    if (result == -1) {
        CHECK_NE(errno, EAGAIN);
        return false;
    }
    else if (result == 0) {
        return true;
    }
    else {
        if (shutdown(fd, direction) == SOCKET_ERROR) {
            return false;
        }
        //adb_shutdown(fd);
        errno = EINVAL;
        return false;

    }
}