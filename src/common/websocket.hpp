#pragma once

#include <arpa/inet.h>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

class WsClient {
  public:
    WsClient() { std::signal(SIGPIPE, SIG_IGN); }
    ~WsClient() { Close(); }

    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;

    bool Connect(const std::string& host, const std::string& port, const std::string& path,
                 const std::string& proxy_host = "", const std::string& proxy_port = "");
    void Shutdown();
    void Close();
    bool Send(const std::string& payload);
    bool Read(std::string& out);
    bool ReadWithTimeout(std::string& out, int timeout_sec);
    bool IsOpen() const { return ssl_ != nullptr; }
    static std::string Base64Encode(const unsigned char* data, size_t len);

  private:
    bool TcpConnect(const std::string& host, const std::string& port);
    bool ProxyConnect(const std::string& proxy_host, const std::string& proxy_port, const std::string& target_host,
                      const std::string& target_port);
    bool TlsHandshake(const std::string& host);
    bool WsHandshake(const std::string& host, const std::string& port, const std::string& path);
    bool RawSend(const void* data, size_t len);
    bool RawRecv(void* buf, size_t len);
    void SendFrame(uint8_t opcode, const void* data, size_t len);
    bool ReadFrame(std::string& out, uint8_t& opcode);

    static std::string GenerateSecKey();

    int fd_ = -1;
    SSL_CTX* ssl_ctx_ = nullptr;
    SSL* ssl_ = nullptr;
};

inline bool WsClient::TcpConnect(const std::string& host, const std::string& port) {
    struct addrinfo hints {
    }, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) {
        return false;
    }
    for (auto* rp = result; rp; rp = rp->ai_next) {
        fd_ = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd_ < 0) {
            continue;
        }
        if (connect(fd_, rp->ai_addr, rp->ai_addrlen) == 0) {
            freeaddrinfo(result);
            return true;
        }
        close(fd_);
        fd_ = -1;
    }
    freeaddrinfo(result);
    return false;
}

inline bool WsClient::ProxyConnect(const std::string& proxy_host, const std::string& proxy_port,
                                   const std::string& target_host, const std::string& target_port) {
    if (!TcpConnect(proxy_host, proxy_port)) {
        return false;
    }
    std::string req = "CONNECT " + target_host + ":" + target_port +
                      " HTTP/1.1\r\n"
                      "Host: " +
                      target_host + ":" + target_port + "\r\n\r\n";
    if (::send(fd_, req.data(), req.size(), 0) <= 0) {
        return false;
    }
    char buf[1024];
    std::string response;
    while (true) {
        ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n <= 0) {
            return false;
        }
        response.append(buf, n);
        if (response.find("\r\n\r\n") != std::string::npos) {
            break;
        }
    }
    return response.find("200") != std::string::npos;
}

inline bool WsClient::TlsHandshake(const std::string& host) {
    ssl_ctx_ = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx_) {
        return false;
    }
    ssl_ = SSL_new(ssl_ctx_);
    SSL_set_tlsext_host_name(ssl_, host.c_str());
    SSL_set_fd(ssl_, fd_);
    return SSL_connect(ssl_) == 1;
}

inline bool WsClient::RawSend(const void* data, size_t len) {
    auto* ptr = static_cast<const char*>(data);
    size_t sent = 0;
    while (sent < len) {
        int n = SSL_write(ssl_, ptr + sent, static_cast<int>(len - sent));
        if (n <= 0) {
            return false;
        }
        sent += n;
    }
    return true;
}

inline bool WsClient::RawRecv(void* buf, size_t len) {
    if (!ssl_) {
        return false;
    }
    auto* ptr = static_cast<char*>(buf);
    size_t received = 0;
    while (received < len) {
        int n = SSL_read(ssl_, ptr + received, static_cast<int>(len - received));
        if (n <= 0) {
            return false;
        }
        received += n;
    }
    return true;
}

inline std::string WsClient::Base64Encode(const unsigned char* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t idx = 0; idx < len; idx += 3) {
        uint32_t block = static_cast<uint32_t>(data[idx]) << 16;
        if (idx + 1 < len) {
            block |= static_cast<uint32_t>(data[idx + 1]) << 8;
        }
        if (idx + 2 < len) {
            block |= static_cast<uint32_t>(data[idx + 2]);
        }
        out.push_back(table[(block >> 18) & 0x3F]);
        out.push_back(table[(block >> 12) & 0x3F]);
        out.push_back((idx + 1 < len) ? table[(block >> 6) & 0x3F] : '=');
        out.push_back((idx + 2 < len) ? table[block & 0x3F] : '=');
    }
    return out;
}

inline std::string WsClient::GenerateSecKey() {
    unsigned char bytes[16];
    RAND_bytes(bytes, sizeof(bytes));
    return Base64Encode(bytes, sizeof(bytes));
}

inline bool WsClient::WsHandshake(const std::string& host, const std::string& port, const std::string& path) {
    std::string key = GenerateSecKey();
    std::string req = "GET " + path +
                      " HTTP/1.1\r\n"
                      "Host: " +
                      host + ":" + port +
                      "\r\n"
                      "Upgrade: websocket\r\n"
                      "Connection: Upgrade\r\n"
                      "Sec-WebSocket-Key: " +
                      key +
                      "\r\n"
                      "Sec-WebSocket-Version: 13\r\n\r\n";
    if (!RawSend(req.data(), req.size())) {
        return false;
    }
    char buf[4096];
    std::string response;
    while (true) {
        int n = SSL_read(ssl_, buf, sizeof(buf));
        if (n <= 0) {
            return false;
        }
        response.append(buf, n);
        if (response.find("\r\n\r\n") != std::string::npos) {
            break;
        }
    }
    return response.find("101") != std::string::npos;
}

inline bool WsClient::Connect(const std::string& host, const std::string& port, const std::string& path,
                              const std::string& proxy_host, const std::string& proxy_port) {
    if (!proxy_host.empty()) {
        if (!ProxyConnect(proxy_host, proxy_port, host, port)) {
            return false;
        }
    } else {
        if (!TcpConnect(host, port)) {
            return false;
        }
    }
    if (!TlsHandshake(host)) {
        return false;
    }
    return WsHandshake(host, port, path);
}

inline void WsClient::Shutdown() {
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
    }
}

inline void WsClient::Close() {
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

inline void WsClient::SendFrame(uint8_t opcode, const void* data, size_t len) {
    uint8_t header[14];
    size_t header_len = 2;
    header[0] = 0x80 | opcode;

    if (len < 126) {
        header[1] = 0x80 | static_cast<uint8_t>(len);
    } else if (len <= 0xFFFF) {
        header[1] = 0x80 | 126;
        header[2] = static_cast<uint8_t>((len >> 8) & 0xFF);
        header[3] = static_cast<uint8_t>(len & 0xFF);
        header_len = 4;
    } else {
        header[1] = 0x80 | 127;
        for (int shift = 7; shift >= 0; --shift) {
            header[header_len++] = static_cast<uint8_t>((len >> (shift * 8)) & 0xFF);
        }
    }

    uint8_t mask_key[4];
    RAND_bytes(mask_key, sizeof(mask_key));
    std::memcpy(header + header_len, mask_key, 4);
    header_len += 4;

    RawSend(header, header_len);

    auto* src = static_cast<const uint8_t*>(data);
    std::vector<uint8_t> masked(len);
    for (size_t idx = 0; idx < len; ++idx) {
        masked[idx] = src[idx] ^ mask_key[idx & 3];
    }
    RawSend(masked.data(), len);
}

inline bool WsClient::Send(const std::string& payload) {
    if (!ssl_) {
        return false;
    }
    SendFrame(0x01, payload.data(), payload.size());
    return true;
}

inline bool WsClient::ReadFrame(std::string& out, uint8_t& opcode) {
    uint8_t head[2];
    if (!RawRecv(head, 2)) {
        return false;
    }
    opcode = head[0] & 0x0F;
    bool masked = (head[1] & 0x80) != 0;
    uint64_t payload_len = head[1] & 0x7F;

    if (payload_len == 126) {
        uint8_t ext[2];
        if (!RawRecv(ext, 2)) {
            return false;
        }
        payload_len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (!RawRecv(ext, 8)) {
            return false;
        }
        payload_len = 0;
        for (int idx = 0; idx < 8; ++idx) {
            payload_len = (payload_len << 8) | ext[idx];
        }
    }

    uint8_t mask_key[4] = {};
    if (masked) {
        if (!RawRecv(mask_key, 4)) {
            return false;
        }
    }

    out.resize(payload_len);
    if (payload_len > 0) {
        if (!RawRecv(out.data(), payload_len)) {
            return false;
        }
        if (masked) {
            for (size_t idx = 0; idx < payload_len; ++idx) {
                out[idx] ^= mask_key[idx & 3];
            }
        }
    }
    return true;
}

inline bool WsClient::Read(std::string& out) {
    while (true) {
        uint8_t opcode;
        if (!ReadFrame(out, opcode)) {
            return false;
        }
        if (opcode == 0x09) {
            SendFrame(0x0A, out.data(), out.size());
            continue;
        }
        if (opcode == 0x08) {
            return false;
        }
        return true;
    }
}

inline bool WsClient::ReadWithTimeout(std::string& out, int timeout_sec) {
    if (fd_ < 0 || !ssl_) {
        return false;
    }

    if (SSL_pending(ssl_) > 0) {
        return Read(out);
    }

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);
    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    int ret = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
    if (ret < 0) {
        return false;
    }
    if (ret == 0) {
        out.clear();
        return true;
    }
    return Read(out);
}
