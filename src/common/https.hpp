#pragma once

#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>
#include <openssl/ssl.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

inline std::string HttpsRequest(const std::string& host, const std::string& port, const std::string& method,
                                const std::string& path, const std::string& headers, const std::string& body = "",
                                const std::string& proxy_host = "", const std::string& proxy_port = "") {
    struct addrinfo hints {
    }, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string connect_host = proxy_host.empty() ? host : proxy_host;
    std::string connect_port = proxy_host.empty() ? port : proxy_port;

    if (getaddrinfo(connect_host.c_str(), connect_port.c_str(), &hints, &result) != 0) {
        return "";
    }

    int fd = -1;
    for (auto* rp = result; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    if (fd < 0) {
        return "";
    }

    if (!proxy_host.empty()) {
        std::string creq = "CONNECT " + host + ":" + port + " HTTP/1.1\r\nHost: " + host + ":" + port + "\r\n\r\n";
        ::send(fd, creq.data(), creq.size(), 0);
        char buf[1024];
        std::string proxy_resp;
        while (true) {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                close(fd);
                return "";
            }
            proxy_resp.append(buf, n);
            if (proxy_resp.find("\r\n\r\n") != std::string::npos) {
                break;
            }
        }
        if (proxy_resp.find("200") == std::string::npos) {
            close(fd);
            return "";
        }
    }

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    SSL* ssl = SSL_new(ctx);
    SSL_set_tlsext_host_name(ssl, host.c_str());
    SSL_set_fd(ssl, fd);
    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return "";
    }

    std::string request = method + " " + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n" + headers;
    if (!body.empty()) {
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    request += "\r\n" + body;
    SSL_write(ssl, request.data(), static_cast<int>(request.size()));

    std::string response;
    char read_buf[4096];
    while (true) {
        int n = SSL_read(ssl, read_buf, sizeof(read_buf));
        if (n <= 0) {
            break;
        }
        response.append(read_buf, n);
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(fd);

    auto header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return "";
    }

    std::string headers_str = response.substr(0, header_end);
    std::string resp_body = response.substr(header_end + 4);

    bool chunked = false;
    for (size_t pos = 0; (pos = headers_str.find("\r\n", pos)) != std::string::npos; pos += 2) {
        auto line_start = pos + 2;
        auto line_end = headers_str.find("\r\n", line_start);
        if (line_end == std::string::npos) line_end = headers_str.size();
        std::string line = headers_str.substr(line_start, line_end - line_start);
        if (line.find("Transfer-Encoding") != std::string::npos && line.find("chunked") != std::string::npos) {
            chunked = true;
            break;
        }
    }

    if (chunked) {
        std::string decoded;
        size_t pos = 0;
        while (pos < resp_body.size()) {
            auto crlf = resp_body.find("\r\n", pos);
            if (crlf == std::string::npos) break;
            size_t chunk_size = std::strtoul(resp_body.data() + pos, nullptr, 16);
            if (chunk_size == 0) break;
            pos = crlf + 2;
            if (pos + chunk_size > resp_body.size()) break;
            decoded.append(resp_body, pos, chunk_size);
            pos += chunk_size + 2;
        }
        return decoded;
    }

    return resp_body;
}
