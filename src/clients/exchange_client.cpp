#include "exchange_client.hpp"

#include "common/logger.hpp"

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

std::string ExchangeClient::HmacSha256Sign(const std::string& key, const std::string& message) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), reinterpret_cast<const unsigned char*>(message.data()),
         message.size(), digest, &digest_len);
    return WsClient::Base64Encode(digest, digest_len);
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
        public_ws_.Send(msg);
    } else {
        std::lock_guard<std::mutex> lock(sub_mutex_);
        pending_subs_.push_back({channel, instrument});
    }
}

void ExchangeClient::SendToPrivate(const std::string& msg) {
    std::lock_guard<std::mutex> lock(sub_mutex_);
    private_ws_.Send(msg);
}

void ExchangeClient::SubscribePrivateChannel(const std::string& channel, const std::string& inst_type) {
    std::string msg = BuildPrivateSubscribeMessage(channel, inst_type);
    SendToPrivate(msg);
}

void ExchangeClient::SendPlaceOrder(const OrderRequest& req) {
    std::string msg = BuildOrderMessage(req);
    SendToPrivate(msg);
}

void ExchangeClient::SendCancelOrder(const std::string& instrument, const std::string& order_id) {
    std::string msg = BuildCancelOrderMessage(instrument, order_id);
    SendToPrivate(msg);
}

void ExchangeClient::Start() {
    running_ = true;

    std::string proxy_host, proxy_port;
    DetectHttpProxy(proxy_host, proxy_port);

    if (!public_ws_.Connect(GetPublicWsHost(), GetPublicWsPort(), GetPublicWsPath(), proxy_host, proxy_port)) {
        throw std::runtime_error("failed to connect public ws");
    }

    {
        std::lock_guard<std::mutex> lock(sub_mutex_);
        for (auto& sub : pending_subs_) {
            public_ws_.Send(BuildSubscribeMessage(sub.channel, sub.instrument));
        }
        pending_subs_.clear();
    }

    while (running_) {
        std::string raw;
        if (!public_ws_.ReadWithTimeout(raw, 15)) {
            break;
        }
        if (raw.empty()) {
            public_ws_.Send("ping");
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
    public_ws_.Shutdown();
    private_ws_.Shutdown();
}

void ExchangeClient::LoginPrivate() {
    running_ = true;

    std::string proxy_host, proxy_port;
    DetectHttpProxy(proxy_host, proxy_port);

    if (!private_ws_.Connect(GetPrivateWsHost(), GetPrivateWsPort(), GetPrivateWsPath(), proxy_host, proxy_port)) {
        throw std::runtime_error("failed to connect private ws");
    }

    private_ws_.Send(BuildLoginMessage());

    std::string response;
    if (!private_ws_.Read(response)) {
        throw std::runtime_error("private ws login read failed");
    }

    Json::Value root = ParseJson(response);
    if (root.get("event", "").asString() != "login" || root.get("code", "-1").asString() != "0") {
        throw std::runtime_error("private ws login failed: " + response);
    }
}

void ExchangeClient::StartPrivateListener() {
    while (running_) {
        std::string raw;
        if (!private_ws_.ReadWithTimeout(raw, 15)) {
            break;
        }
        if (raw.empty()) {
            private_ws_.Send("ping");
            continue;
        }
        if (raw == "pong") {
            continue;
        }
        OnPrivateMessage(raw);
    }
}
