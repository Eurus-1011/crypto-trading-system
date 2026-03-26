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

std::string OkxClient::BuildPrivateSubscribeMessage(const std::string& channel, const std::string& inst_type) {
    Json::Value msg;
    msg["op"] = "subscribe";
    Json::Value arg;
    arg["channel"] = channel;
    arg["instType"] = inst_type;
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

std::string OkxClient::IsoTimestampForRest() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return std::string(buf) + "." + std::to_string(ms.count()) + "Z";
}

std::string OkxClient::Sign(const std::string& timestamp, const std::string& method, const std::string& path) const {
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

    std::string id = std::to_string(NowNs());
    pending_ws_ops_[id] = req;

    Json::Value msg;
    msg["id"] = id;
    msg["op"] = "order";
    Json::Value arg;
    arg["instIdCode"] = inst_id_codes_.count(req.instrument) ? inst_id_codes_.at(req.instrument) : 0;
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

std::string OkxClient::BuildCancelOrderMessage(const std::string& instrument, const std::string& order_id) {
    Json::Value msg;
    msg["id"] = std::to_string(NowNs());
    msg["op"] = "cancel-order";
    Json::Value arg;
    arg["instIdCode"] = inst_id_codes_.count(instrument) ? inst_id_codes_.at(instrument) : 0;
    arg["ordId"] = order_id;
    msg["args"].append(arg);

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, msg);
}

void OkxClient::FetchInstrumentCodes(const std::vector<std::string>& instruments) {
    std::string proxy_host, proxy_port;
    DetectHttpProxy(proxy_host, proxy_port);

    for (const auto& inst : instruments) {
        std::string path = "/api/v5/public/instruments?instType=SPOT&instId=" + inst;
        std::string raw_body = HttpsRequest("www.okx.com", "443", "GET", path, "", "", proxy_host, proxy_port);
        if (raw_body.empty()) {
            WARN("FetchInstrumentCodes failed: [INSTRUMENT] " + inst);
            continue;
        }
        auto json_start = raw_body.find('{');
        if (json_start == std::string::npos) {
            WARN("FetchInstrumentCodes invalid response: [INSTRUMENT] " + inst);
            continue;
        }
        Json::Value root = ParseJson(raw_body.substr(json_start));
        if (root.get("code", "-1").asString() != "0" || !root["data"].isArray() || root["data"].empty()) {
            WARN("FetchInstrumentCodes error: [INSTRUMENT] " + inst + ", [MSG] " + root.get("msg", "").asString());
            continue;
        }
        int code = root["data"][0].get("instIdCode", 0).asInt();
        inst_id_codes_[inst] = code;
        INFO("Fetch instIdCode: [INSTRUMENT] " + inst + ", [CODE] " + std::to_string(code));
    }
}

std::vector<ExecutionReport> OkxClient::QueryPendingOrders(const std::string& inst_type) {
    std::vector<ExecutionReport> result;
    std::string proxy_host, proxy_port;
    DetectHttpProxy(proxy_host, proxy_port);

    std::string after_cursor;
    while (true) {
        std::string path = "/api/v5/trade/orders-pending?instType=" + inst_type;
        if (!after_cursor.empty()) {
            path += "&after=" + after_cursor;
        }

        std::string timestamp = IsoTimestampForRest();
        std::string signature = HmacSha256Sign(config_.secret_key, timestamp + "GET" + path);
        std::string headers = "OK-ACCESS-KEY: " + config_.api_key +
                              "\r\n"
                              "OK-ACCESS-SIGN: " +
                              signature +
                              "\r\n"
                              "OK-ACCESS-TIMESTAMP: " +
                              timestamp +
                              "\r\n"
                              "OK-ACCESS-PASSPHRASE: " +
                              config_.passphrase + "\r\n";

        std::string raw_body = HttpsRequest("www.okx.com", "443", "GET", path, headers, "", proxy_host, proxy_port);
        if (raw_body.empty()) {
            WARN("Query pending orders failed");
            break;
        }

        auto json_start = raw_body.find('{');
        if (json_start == std::string::npos) {
            WARN("Query pending orders: invalid response");
            break;
        }

        Json::Value root = ParseJson(raw_body.substr(json_start));
        if (root.get("code", "-1").asString() != "0") {
            WARN("Query pending orders error: [MSG] " + root.get("msg", "").asString());
            break;
        }

        auto& data = root["data"];
        if (!data.isArray() || data.empty()) {
            break;
        }

        for (Json::ArrayIndex idx = 0; idx < data.size(); ++idx) {
            ExecutionReport report{};
            report.timestamp_ns = NowNs();
            report.SetInstrument(data[idx].get("instId", "").asString().c_str());
            report.SetOrderId(data[idx].get("ordId", "").asString().c_str());
            report.status = MapOkxState(data[idx].get("state", "").asString());
            report.side = (data[idx].get("side", "buy").asString() == "buy") ? Side::BUY : Side::SELL;
            report.price = ParseDouble(data[idx]["px"]);
            report.filled_volume = ParseDouble(data[idx]["accFillSz"]);
            report.total_volume = ParseDouble(data[idx]["sz"]);
            report.avg_fill_price = ParseDouble(data[idx]["avgPx"]);
            result.push_back(report);
        }

        if (data.size() < 100) {
            break;
        }
        after_cursor = data[data.size() - 1].get("ordId", "").asString();
    }

    INFO("Query pending orders complete: [COUNT] " + std::to_string(result.size()));
    return result;
}

std::map<std::string, std::pair<double, double>> OkxClient::QueryBalances() {
    std::string proxy_host, proxy_port;
    DetectHttpProxy(proxy_host, proxy_port);

    std::string path = "/api/v5/account/balance";
    std::string timestamp = IsoTimestampForRest();
    std::string signature = HmacSha256Sign(config_.secret_key, timestamp + "GET" + path);
    std::string headers = "OK-ACCESS-KEY: " + config_.api_key +
                          "\r\n"
                          "OK-ACCESS-SIGN: " +
                          signature +
                          "\r\n"
                          "OK-ACCESS-TIMESTAMP: " +
                          timestamp +
                          "\r\n"
                          "OK-ACCESS-PASSPHRASE: " +
                          config_.passphrase + "\r\n";

    std::string raw_body = HttpsRequest("www.okx.com", "443", "GET", path, headers, "", proxy_host, proxy_port);
    std::map<std::string, std::pair<double, double>> result;
    if (raw_body.empty()) {
        return result;
    }

    auto json_start = raw_body.find('{');
    if (json_start == std::string::npos) {
        return result;
    }

    Json::Value root = ParseJson(raw_body.substr(json_start));
    if (root.get("code", "-1").asString() != "0" || !root["data"].isArray() || root["data"].empty()) {
        return result;
    }

    auto& details = root["data"][0]["details"];
    if (details.isArray()) {
        for (Json::ArrayIndex idx = 0; idx < details.size(); ++idx) {
            std::string currency = details[idx].get("ccy", "").asString();
            double available = ParseDouble(details[idx]["availBal"]);
            double frozen = ParseDouble(details[idx]["frozenBal"]);
            if (available > 0 || frozen > 0) {
                result[currency] = {available, frozen};
            }
        }
    }
    return result;
}

OrderStatus OkxClient::MapOkxState(const std::string& state) {
    if (state == "filled") {
        return OrderStatus::FILLED;
    }
    if (state == "partially_filled") {
        return OrderStatus::PARTIALLY_FILLED;
    }
    if (state == "canceled") {
        return OrderStatus::CANCELLED;
    }
    if (state == "live") {
        return OrderStatus::NEW;
    }
    return OrderStatus::REJECTED;
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

void OkxClient::OnPrivateMessage(const std::string& raw) {
    Json::Value root = ParseJson(raw);

    if (root.isMember("event")) {
        return;
    }

    if (root.isMember("op")) {
        std::string code = root.get("code", "").asString();
        std::string op = root.get("op", "").asString();
        std::string id = root.get("id", "").asString();
        if (code != "0") {
            std::string msg = root.get("msg", "").asString();
            if (root["data"].isArray() && !root["data"].empty()) {
                msg = root["data"][0].get("sMsg", msg).asString();
            }
            ERROR("Private ws op failed: [OP] " + op + ", [CODE] " + code + ", [MSG] " + msg);

            if (op == "order" && on_order_update_ && !id.empty()) {
                auto it = pending_ws_ops_.find(id);
                if (it != pending_ws_ops_.end()) {
                    const auto& req = it->second;
                    ExecutionReport report{};
                    report.timestamp_ns = NowNs();
                    report.SetInstrument(req.instrument.c_str());
                    report.status = OrderStatus::REJECTED;
                    report.side = req.side;
                    if (!req.price.empty()) {
                        report.price = std::stod(req.price);
                    }
                    if (!req.size.empty()) {
                        report.total_volume = std::stod(req.size);
                    }
                    on_order_update_(report);
                    pending_ws_ops_.erase(it);
                }
            }
        } else if (op == "order" && !id.empty()) {
            pending_ws_ops_.erase(id);
        }
        return;
    }

    if (!root.isMember("arg") || !root.isMember("data")) {
        return;
    }

    std::string channel = root["arg"].get("channel", "").asString();
    auto& data = root["data"];

    if (!data.isArray() || data.empty()) {
        return;
    }

    if (channel == OkxChannelOrders) {
        for (Json::ArrayIndex idx = 0; idx < data.size(); ++idx) {
            DecodeOrderUpdate(data[idx]);
        }
    } else if (channel == OkxChannelAccount) {
        for (Json::ArrayIndex idx = 0; idx < data.size(); ++idx) {
            DecodeAccountUpdate(data[idx]);
        }
    }
}

void OkxClient::DecodeOrderUpdate(const Json::Value& data) {
    if (!on_order_update_) {
        return;
    }

    ExecutionReport report{};
    report.timestamp_ns = NowNs();
    report.SetInstrument(data.get("instId", "").asString().c_str());
    report.SetOrderId(data.get("ordId", "").asString().c_str());
    report.status = MapOkxState(data.get("state", "").asString());
    report.side = (data.get("side", "buy").asString() == "buy") ? Side::BUY : Side::SELL;
    report.price = ParseDouble(data["px"]);
    report.filled_volume = ParseDouble(data["accFillSz"]);
    report.total_volume = ParseDouble(data["sz"]);
    report.avg_fill_price = ParseDouble(data["avgPx"]);
    report.fee = ParseDouble(data["fillFee"]);
    report.SetFeeCurrency(data.get("feeCcy", "").asString().c_str());
    on_order_update_(report);
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

void OkxClient::DecodeAccountUpdate(const Json::Value& data) {
    if (!on_balance_update_) {
        return;
    }
    auto& details = data["details"];
    if (!details.isArray()) {
        return;
    }
    for (Json::ArrayIndex idx = 0; idx < details.size(); ++idx) {
        std::string currency = details[idx].get("ccy", "").asString();
        double available = ParseDouble(details[idx]["availBal"]);
        double frozen = ParseDouble(details[idx]["frozenBal"]);
        on_balance_update_(currency, available, frozen);
    }
}
