#pragma once

#include "clients/exchange_client.hpp"
#include "common/config.hpp"
#include "common/logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

static constexpr const char* OkxPublicWsHost  = "ws.okx.com";
static constexpr const char* OkxPublicWsPort  = "443";
static constexpr const char* OkxPublicWsPath  = "/ws/v5/public";
static constexpr const char* OkxPrivateWsHost = "ws.okx.com";
static constexpr const char* OkxPrivateWsPort = "443";
static constexpr const char* OkxPrivateWsPath = "/ws/v5/private";

static constexpr const char* OkxChannelTickers = "tickers";
static constexpr const char* OkxChannelBBO     = "bbo-tbt";
static constexpr const char* OkxChannelBooks5  = "books5";
static constexpr const char* OkxChannelTrades  = "trades";

class OkxClient : public ExchangeClient {
public:
    OkxClient(const ExchangeConfig& config);
    ~OkxClient() override = default;

protected:
    std::string BuildSubscribeMessage(const std::string& channel, const std::string& instrument) override;
    std::string BuildUnsubscribeMessage(const std::string& channel, const std::string& instrument) override;
    std::string BuildLoginMessage() override;
    std::string BuildOrderMessage(const OrderRequest& req) override;
    void OnPublicMessage(const std::string& raw) override;
    OrderResult DecodeOrderResponse(const std::string& raw) override;

    std::string GetPublicWsHost() override;
    std::string GetPublicWsPort() override;
    std::string GetPublicWsPath() override;
    std::string GetPrivateWsHost() override;
    std::string GetPrivateWsPort() override;
    std::string GetPrivateWsPath() override;

private:
    void DecodeTicker(const Json::Value& data, const std::string& instId);
    void DecodeBBO(const Json::Value& data, const std::string& instId);
    void DecodeDepth(const Json::Value& data, const std::string& instId);
    void DecodeTrade(const Json::Value& data, const std::string& instId);

    std::string IsoTimestamp() const;
    std::string Sign(const std::string& timestamp, const std::string& method,
                     const std::string& path) const;

    ExchangeConfig config_;
};
