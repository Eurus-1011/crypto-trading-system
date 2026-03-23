#pragma once

#include "common/quotation.hpp"
#include "common/trading.hpp"
#include "common/utils.hpp"
#include "common/websocket.hpp"

#include <atomic>
#include <cstdlib>
#include <functional>
#include <json/json.h>
#include <mutex>
#include <openssl/hmac.h>
#include <sstream>
#include <string>
#include <vector>

class ExchangeClient {
  public:
    virtual ~ExchangeClient() = default;
    ExchangeClient(const ExchangeClient&) = delete;
    ExchangeClient& operator=(const ExchangeClient&) = delete;

    void OnTicker(std::function<void(const Ticker&)> cb) { on_ticker_ = std::move(cb); }
    void OnBBO(std::function<void(const BBO&)> cb) { on_bbo_ = std::move(cb); }
    void OnDepth(std::function<void(const Depth&)> cb) { on_depth_ = std::move(cb); }
    void OnTrade(std::function<void(const Trade&)> cb) { on_trade_ = std::move(cb); }
    void OnOrderUpdate(std::function<void(const ExecutionReport&)> cb) { on_order_update_ = std::move(cb); }
    void OnBalanceUpdate(std::function<void(const std::string&, double)> cb) { on_balance_update_ = std::move(cb); }

    void Subscribe(const std::string& channel, const std::string& instrument);
    void SendToPrivate(const std::string& msg);
    void SubscribePrivateChannel(const std::string& channel, const std::string& inst_type);
    void SendPlaceOrder(const OrderRequest& req);
    void SendCancelOrder(const std::string& instrument, const std::string& order_id);

    void Start();
    void Stop();
    void LoginPrivate();
    void StartPrivateListener();

  protected:
    ExchangeClient() = default;

    static std::string HmacSha256Sign(const std::string& key, const std::string& message);
    static Json::Value ParseJson(const std::string& raw);
    static bool DetectHttpProxy(std::string& proxy_host, std::string& proxy_port);

    virtual std::string BuildSubscribeMessage(const std::string& channel, const std::string& instrument) = 0;
    virtual std::string BuildUnsubscribeMessage(const std::string& channel, const std::string& instrument) = 0;
    virtual std::string BuildPrivateSubscribeMessage(const std::string& channel, const std::string& inst_type) = 0;
    virtual std::string BuildLoginMessage() = 0;
    virtual std::string BuildOrderMessage(const OrderRequest& req) = 0;
    virtual std::string BuildCancelOrderMessage(const std::string& instrument, const std::string& order_id) = 0;
    virtual void OnPublicMessage(const std::string& raw) = 0;
    virtual void OnPrivateMessage(const std::string& raw) = 0;
    virtual OrderResult DecodeOrderResponse(const std::string& raw) = 0;

    virtual std::string GetPublicWsHost() = 0;
    virtual std::string GetPublicWsPort() = 0;
    virtual std::string GetPublicWsPath() = 0;
    virtual std::string GetPrivateWsHost() = 0;
    virtual std::string GetPrivateWsPort() = 0;
    virtual std::string GetPrivateWsPath() = 0;

    WsClient public_ws_;
    WsClient private_ws_;
    std::atomic<bool> running_{false};

    std::function<void(const Ticker&)> on_ticker_;
    std::function<void(const BBO&)> on_bbo_;
    std::function<void(const Depth&)> on_depth_;
    std::function<void(const Trade&)> on_trade_;
    std::function<void(const ExecutionReport&)> on_order_update_;
    std::function<void(const std::string&, double)> on_balance_update_;

    struct PendingSub {
        std::string channel;
        std::string instrument;
    };
    std::vector<PendingSub> pending_subs_;
    std::mutex sub_mutex_;
};
