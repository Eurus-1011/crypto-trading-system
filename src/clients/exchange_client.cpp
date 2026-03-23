#include "exchange_client.hpp"

bool ExchangeClient::DetectHttpProxy(std::string& proxy_host, std::string& proxy_port) {
    const char* proxy_env = std::getenv("https_proxy");
    if (!proxy_env) {
        proxy_env = std::getenv("HTTPS_PROXY");
    }
    if (!proxy_env) {
        proxy_env = std::getenv("http_proxy");
    }
    if (!proxy_env) {
        proxy_env = std::getenv("HTTP_PROXY");
    }
    if (!proxy_env) {
        return false;
    }

    std::string proxy_url(proxy_env);
    auto scheme_end = proxy_url.find("://");
    if (scheme_end != std::string::npos) {
        proxy_url = proxy_url.substr(scheme_end + 3);
    }
    auto colon_pos = proxy_url.find(':');
    if (colon_pos != std::string::npos) {
        proxy_host = proxy_url.substr(0, colon_pos);
        proxy_port = proxy_url.substr(colon_pos + 1);
    } else {
        proxy_host = proxy_url;
        proxy_port = "3128";
    }
    return true;
}

void ExchangeClient::WsConnect(WsConnection& conn, const std::string& host, const std::string& port,
                               const std::string& path) {
    tcp::resolver resolver(conn.ioc);
    conn.stream = std::make_unique<WsStream>(conn.ioc, conn.ssl_ctx);

    if (!SSL_set_tlsext_host_name(conn.stream->next_layer().native_handle(), host.c_str())) {
        throw beast::system_error(
            beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
            "SSL_set_tlsext_host_name failed");
    }

    auto& tcp_layer = beast::get_lowest_layer(*conn.stream);

    std::string proxy_host, proxy_port;
    if (DetectHttpProxy(proxy_host, proxy_port)) {
        auto proxy_results = resolver.resolve(proxy_host, proxy_port);
        tcp_layer.connect(proxy_results);

        std::string connect_req = "CONNECT " + host + ":" + port + " HTTP/1.1\r\n"
                                  "Host: " + host + ":" + port + "\r\n\r\n";
        auto& raw_socket = tcp_layer.socket();
        net::write(raw_socket, net::buffer(connect_req));

        std::string proxy_response;
        char byte;
        while (true) {
            boost::asio::read(raw_socket, net::buffer(&byte, 1));
            proxy_response.push_back(byte);
            if (proxy_response.size() >= 4 &&
                proxy_response.substr(proxy_response.size() - 4) == "\r\n\r\n") {
                break;
            }
        }

        if (proxy_response.find("200") == std::string::npos) {
            throw std::runtime_error("proxy CONNECT failed: " + proxy_response);
        }
    } else {
        auto results = resolver.resolve(host, port);
        tcp_layer.connect(results);
    }

    tcp_layer.expires_never();
    conn.stream->next_layer().handshake(ssl::stream_base::client);
    conn.stream->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    conn.stream->handshake(host + ":" + port, path);
}

void ExchangeClient::WsSend(WsConnection& conn, const std::string& msg) {
    if (!conn.stream || !conn.stream->is_open()) {
        return;
    }
    std::lock_guard<std::mutex> lock(conn.send_mutex);
    beast::error_code ec;
    conn.stream->write(net::buffer(msg), ec);
}

std::string ExchangeClient::WsRead(WsConnection& conn) {
    beast::error_code ec;
    conn.read_buf.clear();
    conn.stream->read(conn.read_buf, ec);
    if (ec) {
        return "";
    }
    return beast::buffers_to_string(conn.read_buf.data());
}

void ExchangeClient::WsClose(WsConnection& conn) {
    if (conn.stream) {
        beast::error_code ec;
        beast::get_lowest_layer(*conn.stream).socket().shutdown(tcp::socket::shutdown_both, ec);
        beast::get_lowest_layer(*conn.stream).socket().close(ec);
    }
}

std::string ExchangeClient::Base64Encode(const unsigned char* data, int len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (int idx = 0; idx < len; idx += 3) {
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

std::string ExchangeClient::HmacSha256Sign(const std::string& key, const std::string& message) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(message.data()), message.size(), digest, &digest_len);
    return Base64Encode(digest, static_cast<int>(digest_len));
}

Json::Value ExchangeClient::ParseJson(const std::string& raw) {
    Json::CharReaderBuilder builder;
    Json::Value val;
    std::string errs;
    std::istringstream ss(raw);
    Json::parseFromStream(builder, ss, &val, &errs);
    return val;
}

void ExchangeClient::Subscribe(const std::string& channel, const std::string& instrument) {
    std::string msg = BuildSubscribeMessage(channel, instrument);
    if (running_) {
        WsSend(public_ws_, msg);
    } else {
        std::lock_guard<std::mutex> lock(sub_mutex_);
        pending_subs_.push_back({channel, instrument});
    }
}

void ExchangeClient::Start() {
    running_ = true;
    WsConnect(public_ws_, GetPublicWsHost(), GetPublicWsPort(), GetPublicWsPath());

    {
        std::lock_guard<std::mutex> lock(sub_mutex_);
        for (auto& sub : pending_subs_) {
            WsSend(public_ws_, BuildSubscribeMessage(sub.channel, sub.instrument));
        }
        pending_subs_.clear();
    }

    while (running_) {
        std::string raw = WsRead(public_ws_);
        if (raw.empty()) {
            if (running_) {
                break;
            }
            continue;
        }
        if (raw == "pong") {
            continue;
        }
        OnPublicMessage(raw);
    }
}

void ExchangeClient::Stop() {
    running_ = false;
    WsClose(public_ws_);
    WsClose(private_ws_);
}

void ExchangeClient::LoginPrivate() {
    WsConnect(private_ws_, GetPrivateWsHost(), GetPrivateWsPort(), GetPrivateWsPath());
    WsSend(private_ws_, BuildLoginMessage());

    std::string response = WsRead(private_ws_);
    Json::Value root = ParseJson(response);
    if (root.get("event", "").asString() != "login" || root.get("code", "-1").asString() != "0") {
        throw std::runtime_error("private ws login failed: " + response);
    }
}

OrderResult ExchangeClient::PlaceOrder(const OrderRequest& req) {
    std::string order_msg = BuildOrderMessage(req);
    WsSend(private_ws_, order_msg);
    std::string response = WsRead(private_ws_);
    return DecodeOrderResponse(response);
}
