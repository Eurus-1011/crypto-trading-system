#pragma once

#include "common/quotation.hpp"
#include "common/trading.hpp"
#include "common/utils.hpp"

#include <atomic>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <functional>
#include <json/json.h>
#include <memory>
#include <mutex>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <string>
#include <vector>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

class ExchangeClient {
public:
    using WsStream = websocket::stream<beast::ssl_stream<beast::tcp_stream>>;

    virtual ~ExchangeClient() = default;
    ExchangeClient(const ExchangeClient&) = delete;
    ExchangeClient& operator=(const ExchangeClient&) = delete;

    void OnTicker(std::function<void(const Ticker&)> cb) { on_ticker_ = std::move(cb); }
    void OnBBO(std::function<void(const BBO&)> cb) { on_bbo_ = std::move(cb); }
    void OnDepth(std::function<void(const Depth&)> cb) { on_depth_ = std::move(cb); }
    void OnTrade(std::function<void(const Trade&)> cb) { on_trade_ = std::move(cb); }

    void Subscribe(const std::string& channel, const std::string& instrument);
    OrderResult PlaceOrder(const OrderRequest& req);

    void Start();
    void Stop();
    void LoginPrivate();

protected:
    ExchangeClient() = default;

    struct WsConnection {
        net::io_context ioc;
        ssl::context ssl_ctx{ssl::context::tlsv12_client};
        std::unique_ptr<WsStream> stream;
        beast::flat_buffer read_buf;
        std::mutex send_mutex;

        WsConnection() {
            ssl_ctx.set_default_verify_paths();
            ssl_ctx.set_verify_mode(ssl::verify_none);
        }
    };

    void WsConnect(WsConnection& conn, const std::string& host, const std::string& port,
                   const std::string& path);
    static bool DetectHttpProxy(std::string& proxy_host, std::string& proxy_port);
    void WsSend(WsConnection& conn, const std::string& msg);
    std::string WsRead(WsConnection& conn);
    void WsClose(WsConnection& conn);

    static std::string Base64Encode(const unsigned char* data, int len);
    static std::string HmacSha256Sign(const std::string& key, const std::string& message);
    static Json::Value ParseJson(const std::string& raw);

    virtual std::string BuildSubscribeMessage(const std::string& channel, const std::string& instrument) = 0;
    virtual std::string BuildUnsubscribeMessage(const std::string& channel, const std::string& instrument) = 0;
    virtual std::string BuildLoginMessage() = 0;
    virtual std::string BuildOrderMessage(const OrderRequest& req) = 0;
    virtual void OnPublicMessage(const std::string& raw) = 0;
    virtual OrderResult DecodeOrderResponse(const std::string& raw) = 0;

    virtual std::string GetPublicWsHost() = 0;
    virtual std::string GetPublicWsPort() = 0;
    virtual std::string GetPublicWsPath() = 0;
    virtual std::string GetPrivateWsHost() = 0;
    virtual std::string GetPrivateWsPort() = 0;
    virtual std::string GetPrivateWsPath() = 0;

    WsConnection public_ws_;
    WsConnection private_ws_;
    std::atomic<bool> running_{false};

    std::function<void(const Ticker&)> on_ticker_;
    std::function<void(const BBO&)>    on_bbo_;
    std::function<void(const Depth&)>  on_depth_;
    std::function<void(const Trade&)>  on_trade_;

    struct PendingSub {
        std::string channel;
        std::string instrument;
    };
    std::vector<PendingSub> pending_subs_;
    std::mutex sub_mutex_;
};
