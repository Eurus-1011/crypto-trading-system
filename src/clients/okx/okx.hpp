#pragma once

#include "clients/exchange_client.hpp"
#include "common/config.hpp"
#include "common/https.hpp"
#include "common/logger.hpp"

#include <chrono>
#include <ctime>
#include <map>
#include <string>
#include <vector>

static constexpr const char* OkxPublicWsHost = "ws.okx.com";
static constexpr const char* OkxPublicWsPort = "443";
static constexpr const char* OkxPublicWsPath = "/ws/v5/public";
static constexpr const char* OkxPrivateWsHost = "ws.okx.com";
static constexpr const char* OkxPrivateWsPort = "443";
static constexpr const char* OkxPrivateWsPath = "/ws/v5/private";
static constexpr const char* OkxChannelTickers = "tickers";
static constexpr const char* OkxChannelBBO = "bbo-tbt";
static constexpr const char* OkxChannelBooks5 = "books5";
static constexpr const char* OkxChannelTrades = "trades";
static constexpr const char* OkxChannelOrders = "orders";
static constexpr const char* OkxChannelAccount = "account";

class OkxClient : public ExchangeClient {
  public:
    OkxClient(const ExchangeConfig& config);
    ~OkxClient() override = default;

  protected:
    std::string BuildSubscribeMessage(const std::string& channel, const std::string& instrument) override;
    std::string BuildPrivateSubscribeMessage(const std::string& channel, const std::string& inst_type) override;
    std::string BuildLoginMessage() override;
    std::string BuildOrderMessage(const OrderRequest& req) override;
    std::string BuildCancelOrderMessage(const std::string& instrument, const std::string& order_id) override;
    void OnPublicMessage(const std::string& raw) override;
    void OnPrivateMessage(const std::string& raw) override;

    std::string GetPublicWsHost() override;
    std::string GetPublicWsPort() override;
    std::string GetPublicWsPath() override;
    std::string GetPrivateWsHost() override;
    std::string GetPrivateWsPort() override;
    std::string GetPrivateWsPath() override;

  public:
    std::vector<ExecutionReport> QueryPendingOrders(const std::string& inst_type);
    std::map<std::string, std::pair<double, double>> QueryBalances();

  private:
    void DecodeTicker(const Json::Value& data, const std::string& instId);
    void DecodeBBO(const Json::Value& data, const std::string& instId);
    void DecodeDepth(const Json::Value& data, const std::string& instId);
    void DecodeTrade(const Json::Value& data, const std::string& instId);
    void DecodeOrderUpdate(const Json::Value& data);
    void DecodeAccountUpdate(const Json::Value& data);

    std::string IsoTimestamp() const;
    std::string IsoTimestampForRest() const;
    std::string Sign(const std::string& timestamp, const std::string& method, const std::string& path) const;

    static OrderStatus MapOkxState(const std::string& state);

    ExchangeConfig config_;
};
