#pragma once

#include "clients/exchange_client.hpp"
#include "common/config.hpp"

#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

static constexpr const char* OkxRestHost = "www.okx.com";
static constexpr const char* OkxRestPort = "443";
static constexpr const char* OkxWsHost = "ws.okx.com";
static constexpr const char* OkxWsPort = "443";
static constexpr const char* OkxPublicWsPath = "/ws/v5/public";
static constexpr const char* OkxPrivateWsPath = "/ws/v5/private";
static constexpr const char* OkxApiInstruments = "/api/v5/public/instruments";
static constexpr const char* OkxApiOrdersPending = "/api/v5/trade/orders-pending";
static constexpr const char* OkxApiOrder = "/api/v5/trade/order";
static constexpr const char* OkxApiFills = "/api/v5/trade/fills";
static constexpr const char* OkxApiBalance = "/api/v5/account/balance";
static constexpr const char* OkxApiPositions = "/api/v5/account/positions";
static constexpr const char* OkxChannelTickers = "tickers";
static constexpr const char* OkxChannelBBO = "bbo-tbt";
static constexpr const char* OkxChannelBooks5 = "books5";
static constexpr const char* OkxChannelTrades = "trades";
static constexpr const char* OkxChannelOrders = "orders";
static constexpr const char* OkxChannelAccount = "account";

inline const std::unordered_map<Side, const char*> kOkxSide = {
    {Side::BUY, "buy"},
    {Side::SELL, "sell"},
};
inline const std::unordered_map<PosSide, const char*> kOkxPosSide = {
    {PosSide::NET, "net"},
    {PosSide::LONG, "long"},
    {PosSide::SHORT, "short"},
};
inline const std::unordered_map<OrderType, const char*> kOkxOrderType = {
    {OrderType::MARKET, "market"},
    {OrderType::LIMIT, "limit"},
};
inline const std::unordered_map<TradeMode, const char*> kOkxTradeMode = {
    {TradeMode::CASH, "cash"},
    {TradeMode::CROSS, "cross"},
    {TradeMode::ISOLATED, "isolated"},
};
inline const std::unordered_map<MarketType, const char*> kOkxInstType = {
    {MarketType::SPOT, "SPOT"},
    {MarketType::SWAP, "SWAP"},
};

inline const std::unordered_map<std::string_view, Side> kOkxSideEnum = {
    {"buy", Side::BUY},
    {"sell", Side::SELL},
};
inline const std::unordered_map<std::string_view, PosSide> kOkxPosSideEnum = {
    {"net", PosSide::NET},
    {"long", PosSide::LONG},
    {"short", PosSide::SHORT},
};
inline const std::unordered_map<std::string_view, MarketType> kOkxInstTypeEnum = {
    {"SPOT", MarketType::SPOT},
    {"SWAP", MarketType::SWAP},
};
inline const std::unordered_map<std::string_view, OrderStatus> kOkxOrderStateEnum = {
    {"filled", OrderStatus::FILLED},
    {"partially_filled", OrderStatus::PARTIALLY_FILLED},
    {"canceled", OrderStatus::CANCELLED},
    {"live", OrderStatus::NEW},
};
inline const std::unordered_map<std::string_view, TradeMode> kOkxTradeModeEnum = {
    {"cash", TradeMode::CASH},
    {"cross", TradeMode::CROSS},
    {"isolated", TradeMode::ISOLATED},
};

inline Side OkxParseSide(std::string_view s) {
    auto it = kOkxSideEnum.find(s);
    return it != kOkxSideEnum.end() ? it->second : Side::BUY;
}
inline PosSide OkxParsePosSide(std::string_view s) {
    auto it = kOkxPosSideEnum.find(s);
    return it != kOkxPosSideEnum.end() ? it->second : PosSide::NET;
}
inline MarketType OkxParseInstType(std::string_view s) {
    auto it = kOkxInstTypeEnum.find(s);
    return it != kOkxInstTypeEnum.end() ? it->second : MarketType::SPOT;
}
inline OrderStatus OkxParseOrderState(std::string_view s) {
    auto it = kOkxOrderStateEnum.find(s);
    return it != kOkxOrderStateEnum.end() ? it->second : OrderStatus::REJECTED;
}
inline TradeMode OkxParseTradeMode(std::string_view s) {
    auto it = kOkxTradeModeEnum.find(s);
    return it != kOkxTradeModeEnum.end() ? it->second : TradeMode::CASH;
}

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
    bool ParseLoginResponse(const std::string& response) override;

    std::string GetPublicWsHost() override;
    std::string GetPublicWsPort() override;
    std::string GetPublicWsPath() override;
    std::string GetPrivateWsHost() override;
    std::string GetPrivateWsPort() override;
    std::string GetPrivateWsPath() override;

  public:
    std::vector<ExecutionReport> QuerySpotPendingOrders();
    std::vector<ExecutionReport> QuerySwapPendingOrders();
    bool QueryOrderById(const std::string& instrument, const std::string& order_id, ExecutionReport& report);
    std::vector<Fill> QueryRecentFills(int64_t since_ms, const std::string& inst_type);
    std::map<std::string, std::tuple<double, double, double>> QueryBalances();
    std::map<std::string, std::map<PosSide, SwapPosition>> QuerySwapPositions();
    void FetchInstrumentInfo(const std::vector<std::string>& instruments);

  private:
    std::vector<ExecutionReport> QueryPendingOrdersByType(const std::string& inst_type);

    void DecodeTicker(simdjson::ondemand::value data, const std::string& inst_id);
    void DecodeBBO(simdjson::ondemand::value data, const std::string& inst_id);
    void DecodeDepth(simdjson::ondemand::value data, const std::string& inst_id);
    void DecodeTrade(simdjson::ondemand::value data, const std::string& inst_id);
    void DecodeOrderUpdate(simdjson::ondemand::value data);
    void DecodeAccountUpdate(simdjson::ondemand::value data);

    std::string IsoTimestamp() const;
    std::string IsoTimestampForRest() const;
    std::string Sign(const std::string& timestamp, const std::string& method, const std::string& path) const;

    ExchangeConfig config_;
    std::map<std::string, int> inst_id_codes_;
    std::map<std::string, OrderRequest> pending_ws_ops_;
    simdjson::ondemand::parser public_parser_;
    simdjson::ondemand::parser private_parser_;
};
