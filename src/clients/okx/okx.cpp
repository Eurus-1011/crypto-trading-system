#include "okx.hpp"

OkxClient::OkxClient(const ExchangeConfig& config) : config_(config) {}

std::string OkxClient::GetPublicWsHost() { return OkxPublicWsHost; }
std::string OkxClient::GetPublicWsPort() { return OkxPublicWsPort; }
std::string OkxClient::GetPublicWsPath() { return OkxPublicWsPath; }
std::string OkxClient::GetPrivateWsHost() { return OkxPrivateWsHost; }
std::string OkxClient::GetPrivateWsPort() { return OkxPrivateWsPort; }
std::string OkxClient::GetPrivateWsPath() { return OkxPrivateWsPath; }

std::string OkxClient::BuildSubscribeMessage(const std::string& channel, const std::string& instrument) {
    Json::Value msg;
    msg["op"] = "subscribe";
    Json::Value arg;
    arg["channel"] = channel;
    arg["instId"] = instrument;
    msg["args"].append(arg);
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, msg);
}

std::string OkxClient::BuildUnsubscribeMessage(const std::string& channel, const std::string& instrument) {
    Json::Value msg;
    msg["op"] = "unsubscribe";
    Json::Value arg;
    arg["channel"] = channel;
    arg["instId"] = instrument;
    msg["args"].append(arg);
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, msg);
}

std::string OkxClient::IsoTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    return std::to_string(seconds.count());
}

std::string OkxClient::Sign(const std::string& timestamp, const std::string& method,
                            const std::string& path) const {
    std::string prehash = timestamp + method + path;
    return HmacSha256Sign(config_.secret_key, prehash);
}

std::string OkxClient::BuildLoginMessage() {
    std::string timestamp = IsoTimestamp();
    std::string signature = Sign(timestamp, "GET", "/users/self/verify");

    Json::Value msg;
    msg["op"] = "login";
    Json::Value arg;
    arg["apiKey"] = config_.api_key;
    arg["passphrase"] = config_.passphrase;
    arg["timestamp"] = timestamp;
    arg["sign"] = signature;
    msg["args"].append(arg);

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, msg);
}

std::string OkxClient::BuildOrderMessage(const OrderRequest& req) {
    std::string side_str = (req.side == Side::BUY) ? "buy" : "sell";
    std::string order_type = (req.order_type == OrderType::MARKET) ? "market" : "limit";

    Json::Value msg;
    msg["id"] = std::to_string(NowNs());
    msg["op"] = "order";
    Json::Value arg;
    arg["instId"] = req.instrument;
    arg["tdMode"] = "cash";
    arg["side"] = side_str;
    arg["ordType"] = order_type;
    arg["sz"] = req.size;
    if (req.order_type == OrderType::MARKET && req.side == Side::BUY) {
        arg["tgtCcy"] = req.target_currency.empty() ? "quote_ccy" : req.target_currency;
    }
    if (req.order_type == OrderType::LIMIT && !req.price.empty()) {
        arg["px"] = req.price;
    }
    msg["args"].append(arg);

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, msg);
}

void OkxClient::OnPublicMessage(const std::string& raw) {
    Json::Value root = ParseJson(raw);
    if (!root.isMember("arg") || !root.isMember("data")) {
        return;
    }

    std::string channel = root["arg"].get("channel", "").asString();
    std::string instId = root["arg"].get("instId", "").asString();
    auto& data = root["data"];

    if (!data.isArray() || data.empty()) {
        return;
    }

    if (channel == OkxChannelTickers) {
        DecodeTicker(data[0], instId);
    } else if (channel == OkxChannelBBO) {
        DecodeBBO(data[0], instId);
    } else if (channel == OkxChannelBooks5) {
        DecodeDepth(data[0], instId);
    } else if (channel == OkxChannelTrades) {
        for (Json::ArrayIndex idx = 0; idx < data.size(); ++idx) {
            DecodeTrade(data[idx], instId);
        }
    }
}

void OkxClient::DecodeTicker(const Json::Value& data, const std::string& instId) {
    if (!on_ticker_) {
        return;
    }
    Ticker ticker{};
    ticker.exchange_ts_ms = ParseUint64(data["ts"]);
    ticker.local_ts_ns = NowNs();
    ticker.SetInstrument(instId.c_str());
    ticker.last_price = ParseDouble(data["last"]);
    ticker.last_volume = ParseDouble(data["lastSz"]);
    ticker.bid_price = ParseDouble(data["bidPx"]);
    ticker.bid_volume = ParseDouble(data["bidSz"]);
    ticker.ask_price = ParseDouble(data["askPx"]);
    ticker.ask_volume = ParseDouble(data["askSz"]);
    ticker.open_24h = ParseDouble(data["open24h"]);
    ticker.high_24h = ParseDouble(data["high24h"]);
    ticker.low_24h = ParseDouble(data["low24h"]);
    ticker.volume_24h = ParseDouble(data["vol24h"]);
    ticker.volume_currency_24h = ParseDouble(data["volCcy24h"]);
    on_ticker_(ticker);
}

void OkxClient::DecodeBBO(const Json::Value& data, const std::string& instId) {
    if (!on_bbo_) {
        return;
    }
    BBO bbo{};
    bbo.exchange_ts_ms = ParseUint64(data["ts"]);
    bbo.local_ts_ns = NowNs();
    bbo.SetInstrument(instId.c_str());

    auto& bids = data["bids"];
    auto& asks = data["asks"];
    if (bids.isArray() && !bids.empty()) {
        bbo.bid_price = ParseDouble(bids[0][0]);
        bbo.bid_volume = ParseDouble(bids[0][1]);
    }
    if (asks.isArray() && !asks.empty()) {
        bbo.ask_price = ParseDouble(asks[0][0]);
        bbo.ask_volume = ParseDouble(asks[0][1]);
    }
    on_bbo_(bbo);
}

void OkxClient::DecodeDepth(const Json::Value& data, const std::string& instId) {
    if (!on_depth_) {
        return;
    }
    Depth depth{};
    depth.exchange_ts_ms = ParseUint64(data["ts"]);
    depth.local_ts_ns = NowNs();
    depth.SetInstrument(instId.c_str());

    auto& asks = data["asks"];
    auto& bids = data["bids"];
    depth.ask_levels = 0;
    if (asks.isArray()) {
        for (Json::ArrayIndex idx = 0; idx < asks.size() && idx < MAX_DEPTH_LEVELS; ++idx) {
            depth.asks[idx].price = ParseDouble(asks[idx][0]);
            depth.asks[idx].volume = ParseDouble(asks[idx][1]);
            depth.asks[idx].order_count = static_cast<int>(ParseUint64(asks[idx][3]));
            ++depth.ask_levels;
        }
    }
    depth.bid_levels = 0;
    if (bids.isArray()) {
        for (Json::ArrayIndex idx = 0; idx < bids.size() && idx < MAX_DEPTH_LEVELS; ++idx) {
            depth.bids[idx].price = ParseDouble(bids[idx][0]);
            depth.bids[idx].volume = ParseDouble(bids[idx][1]);
            depth.bids[idx].order_count = static_cast<int>(ParseUint64(bids[idx][3]));
            ++depth.bid_levels;
        }
    }
    on_depth_(depth);
}

void OkxClient::DecodeTrade(const Json::Value& data, const std::string& instId) {
    if (!on_trade_) {
        return;
    }
    Trade trade{};
    trade.exchange_ts_ms = ParseUint64(data["ts"]);
    trade.local_ts_ns = NowNs();
    trade.SetInstrument(instId.c_str());
    std::string tradeIdStr = data.get("tradeId", "").asString();
    std::strncpy(trade.trade_id, tradeIdStr.c_str(), sizeof(trade.trade_id) - 1);
    trade.trade_id[sizeof(trade.trade_id) - 1] = '\0';
    trade.price = ParseDouble(data["px"]);
    trade.volume = ParseDouble(data["sz"]);
    trade.side = (data.get("side", "buy").asString() == "buy") ? TradeSide::BUY : TradeSide::SELL;
    on_trade_(trade);
}

OrderResult OkxClient::DecodeOrderResponse(const std::string& raw) {
    Json::Value root = ParseJson(raw);
    OrderResult result{};
    result.code = root.get("code", "-1").asString();
    result.success = (result.code == "0");
    if (result.success && root["data"].isArray() && !root["data"].empty()) {
        auto& orderData = root["data"][0];
        result.order_id = orderData.get("ordId", "").asString();
        result.message = orderData.get("sMsg", "").asString();
    } else {
        result.message = root.get("msg", "unknown error").asString();
    }
    return result;
}
