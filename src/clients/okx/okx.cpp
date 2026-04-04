#include "okx.hpp"

#include "common/https.hpp"
#include "common/logger.hpp"
#include "common/utils.hpp"

OkxClient::OkxClient(const ExchangeConfig& config) : config_(config) {}

std::string OkxClient::GetPublicWsHost() { return OkxWsHost; }
std::string OkxClient::GetPublicWsPort() { return OkxWsPort; }
std::string OkxClient::GetPublicWsPath() { return OkxPublicWsPath; }
std::string OkxClient::GetPrivateWsHost() { return OkxWsHost; }
std::string OkxClient::GetPrivateWsPort() { return OkxWsPort; }
std::string OkxClient::GetPrivateWsPath() { return OkxPrivateWsPath; }

std::string OkxClient::BuildSubscribeMessage(const std::string& channel, const std::string& instrument) {
    return R"({"op":"subscribe","args":[{"channel":")" + channel + R"(","instId":")" + instrument + R"("}]})";
}

std::string OkxClient::BuildPrivateSubscribeMessage(const std::string& channel, const std::string& inst_type) {
    return R"({"op":"subscribe","args":[{"channel":")" + channel + R"(","instType":")" + inst_type + R"("}]})";
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

    std::string msg;
    msg.reserve(256);
    msg += R"({"op":"login","args":[{"apiKey":")";
    msg += config_.api_key;
    msg += R"(","passphrase":")";
    msg += config_.passphrase;
    msg += R"(","timestamp":")";
    msg += timestamp;
    msg += R"(","sign":")";
    msg += signature;
    msg += R"("}]})";
    return msg;
}

std::string OkxClient::BuildOrderMessage(const OrderRequest& req) {
    std::string id = std::to_string(NowNs());
    pending_ws_ops_[id] = req;

    int inst_id_code = inst_id_codes_.count(req.instrument) ? inst_id_codes_.at(req.instrument) : 0;

    std::string msg;
    msg.reserve(512);
    msg += R"({"id":")";
    msg += id;
    msg += R"(","op":"order","args":[{"instIdCode":)";
    msg += std::to_string(inst_id_code);
    msg += R"(,"side":")";
    msg += kOkxSide.at(req.side);
    msg += R"(","ordType":")";
    msg += kOkxOrderType.at(req.order_type);
    msg += R"(","sz":")";
    msg += req.size;
    msg += R"(","tdMode":")";
    msg += kOkxTdMode.at(req.market_type);
    msg += '"';

    if (req.market_type == MarketType::SWAP) {
        msg += R"(,"posSide":")";
        msg += kOkxPosSide.at(req.position_side);
        msg += '"';
    } else if (req.order_type == OrderType::MARKET && req.side == Side::BUY) {
        msg += R"(,"tgtCcy":")";
        msg += req.target_currency.empty() ? "quote_ccy" : req.target_currency;
        msg += '"';
    }

    if (req.order_type == OrderType::LIMIT && !req.price.empty()) {
        msg += R"(,"px":")";
        msg += req.price;
        msg += '"';
    }
    msg += R"(}]})";
    return msg;
}

std::string OkxClient::BuildCancelOrderMessage(const std::string& instrument, const std::string& order_id) {
    int inst_id_code = inst_id_codes_.count(instrument) ? inst_id_codes_.at(instrument) : 0;

    std::string msg;
    msg.reserve(256);
    msg += R"({"id":")";
    msg += std::to_string(NowNs());
    msg += R"(","op":"cancel-order","args":[{"instIdCode":)";
    msg += std::to_string(inst_id_code);
    msg += R"(,"ordId":")";
    msg += order_id;
    msg += R"("}]})";
    return msg;
}

bool OkxClient::ParseLoginResponse(const std::string& response) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(response);
    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc)) {
        return false;
    }
    std::string_view event_value, code_value;
    if (doc["event"].get(event_value) || event_value != "login") {
        return false;
    }
    if (doc["code"].get(code_value) || code_value != "0") {
        return false;
    }
    return true;
}

void OkxClient::FetchInstrumentCodes(const std::vector<std::string>& instruments) {
    std::string proxy_host, proxy_port;
    DetectHttpProxy(proxy_host, proxy_port);

    simdjson::ondemand::parser parser;
    for (const auto& instrument : instruments) {
        const char* inst_type = kOkxInstType.at(DetectMarketType(instrument.c_str()));
        std::string path = std::string("/api/v5/public/instruments?instType=") + inst_type + "&instId=" + instrument;
        std::string raw_body = HttpsRequest("www.okx.com", "443", "GET", path, "", "", proxy_host, proxy_port);
        if (raw_body.empty()) {
            WARN("FetchInstrumentCodes failed: [INSTRUMENT] " + instrument);
            continue;
        }
        auto json_start = raw_body.find('{');
        if (json_start == std::string::npos) {
            WARN("FetchInstrumentCodes invalid response: [INSTRUMENT] " + instrument);
            continue;
        }

        simdjson::padded_string padded_body(raw_body.data() + json_start, raw_body.size() - json_start);
        simdjson::ondemand::document doc;
        if (parser.iterate(padded_body).get(doc)) {
            WARN("FetchInstrumentCodes parse failed: [INSTRUMENT] " + instrument);
            continue;
        }

        std::string_view code_str;
        if (doc["code"].get(code_str) || code_str != "0") {
            std::string_view msg_str;
            (void)doc["msg"].get(msg_str);
            WARN("FetchInstrumentCodes error: [INSTRUMENT] " + instrument + ", [MSG] " + std::string(msg_str));
            continue;
        }

        simdjson::ondemand::array data_array;
        if (doc["data"].get_array().get(data_array)) {
            WARN("FetchInstrumentCodes empty data: [INSTRUMENT] " + instrument);
            continue;
        }

        for (auto element : data_array) {
            int64_t code_val = 0;
            (void)element["instIdCode"].get_int64().get(code_val);
            inst_id_codes_[instrument] = static_cast<int>(code_val);
            INFO("Fetch instIdCode: [INSTRUMENT] " + instrument + ", [CODE] " + std::to_string(code_val));
            break;
        }
    }
}

std::vector<ExecutionReport> OkxClient::QueryPendingOrdersByType(const std::string& inst_type) {
    std::vector<ExecutionReport> result;
    std::string proxy_host, proxy_port;
    DetectHttpProxy(proxy_host, proxy_port);

    simdjson::ondemand::parser parser;
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
            WARN("Query pending orders failed: [INST_TYPE] " + inst_type);
            break;
        }

        auto json_start = raw_body.find('{');
        if (json_start == std::string::npos) {
            WARN("Query pending orders: invalid response, [INST_TYPE] " + inst_type);
            break;
        }

        simdjson::padded_string padded_body(raw_body.data() + json_start, raw_body.size() - json_start);
        simdjson::ondemand::document doc;
        if (parser.iterate(padded_body).get(doc)) {
            WARN("Query pending orders parse failed: [INST_TYPE] " + inst_type);
            break;
        }

        std::string_view code_str;
        if (doc["code"].get(code_str) || code_str != "0") {
            std::string_view msg_str;
            (void)doc["msg"].get(msg_str);
            WARN("Query pending orders error: [INST_TYPE] " + inst_type + ", [MSG] " + std::string(msg_str));
            break;
        }

        simdjson::ondemand::array data_array;
        if (doc["data"].get_array().get(data_array)) {
            break;
        }

        MarketType market_type = OkxParseInstType(inst_type);
        size_t data_count = 0;
        std::string last_order_id;

        for (auto order_result : data_array) {
            simdjson::ondemand::value order_element;
            if (order_result.get(order_element)) {
                break;
            }

            std::string_view inst_id, order_id, state, side, pos_side;
            (void)order_element["instId"].get(inst_id);
            (void)order_element["ordId"].get(order_id);
            (void)order_element["state"].get(state);
            (void)order_element["side"].get(side);
            (void)order_element["posSide"].get(pos_side);

            std::string order_id_str(order_id);

            ExecutionReport report{};
            report.timestamp_ns = NowNs();
            report.SetInstrument(std::string(inst_id).c_str());
            report.SetOrderId(order_id_str.c_str());
            report.status = OkxParseOrderState(state);
            report.side = OkxParseSide(side.empty() ? "buy" : side);
            report.market_type = market_type;
            report.position_side = OkxParsePosSide(pos_side.empty() ? "net" : pos_side);
            (void)order_element["px"].get_double_in_string().get(report.price);
            (void)order_element["accFillSz"].get_double_in_string().get(report.filled_volume);
            (void)order_element["sz"].get_double_in_string().get(report.total_volume);
            (void)order_element["avgPx"].get_double_in_string().get(report.avg_fill_price);
            result.push_back(report);

            last_order_id = std::move(order_id_str);
            ++data_count;
        }

        if (data_count < 100) {
            break;
        }
        after_cursor = last_order_id;
    }
    return result;
}

std::vector<ExecutionReport> OkxClient::QuerySpotPendingOrders() {
    auto result = QueryPendingOrdersByType("SPOT");
    INFO("Query spot pending orders complete: [COUNT] " + std::to_string(result.size()));
    return result;
}

std::vector<ExecutionReport> OkxClient::QuerySwapPendingOrders() {
    auto result = QueryPendingOrdersByType("SWAP");
    INFO("Query swap pending orders complete: [COUNT] " + std::to_string(result.size()));
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

    simdjson::ondemand::parser parser;
    simdjson::padded_string padded_body(raw_body.data() + json_start, raw_body.size() - json_start);
    simdjson::ondemand::document doc;
    if (parser.iterate(padded_body).get(doc)) {
        return result;
    }

    std::string_view code_str;
    if (doc["code"].get(code_str) || code_str != "0") {
        return result;
    }

    simdjson::ondemand::array data_array;
    if (doc["data"].get_array().get(data_array)) {
        return result;
    }

    for (auto first_result : data_array) {
        simdjson::ondemand::value first_element;
        if (first_result.get(first_element)) {
            break;
        }

        simdjson::ondemand::array details_array;
        if (first_element["details"].get_array().get(details_array)) {
            break;
        }

        for (auto detail_result : details_array) {
            simdjson::ondemand::value detail;
            if (detail_result.get(detail)) {
                continue;
            }
            std::string_view currency;
            if (detail["ccy"].get(currency) || currency.empty()) {
                continue;
            }
            double available = 0.0, frozen = 0.0;
            (void)detail["availBal"].get_double_in_string().get(available);
            (void)detail["frozenBal"].get_double_in_string().get(frozen);
            if (available > 0 || frozen > 0) {
                result[std::string(currency)] = {available, frozen};
            }
        }
        break;
    }
    return result;
}

std::map<std::string, std::map<PosSide, SwapPosition>> OkxClient::QuerySwapPositions() {
    std::string proxy_host, proxy_port;
    DetectHttpProxy(proxy_host, proxy_port);

    std::string path = "/api/v5/account/positions?instType=SWAP";
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
    std::map<std::string, std::map<PosSide, SwapPosition>> result;
    if (raw_body.empty()) {
        return result;
    }

    auto json_start = raw_body.find('{');
    if (json_start == std::string::npos) {
        return result;
    }

    simdjson::ondemand::parser parser;
    simdjson::padded_string padded_body(raw_body.data() + json_start, raw_body.size() - json_start);
    simdjson::ondemand::document doc;
    if (parser.iterate(padded_body).get(doc)) {
        return result;
    }

    std::string_view code_str;
    if (doc["code"].get(code_str) || code_str != "0") {
        return result;
    }

    simdjson::ondemand::array data_array;
    if (doc["data"].get_array().get(data_array)) {
        return result;
    }

    for (auto position_result : data_array) {
        simdjson::ondemand::value position_element;
        if (position_result.get(position_element)) {
            break;
        }

        std::string_view inst_id, pos_side_str;
        (void)position_element["instId"].get(inst_id);
        (void)position_element["posSide"].get(pos_side_str);

        std::string instrument(inst_id);
        PosSide position_side = OkxParsePosSide(pos_side_str.empty() ? "net" : pos_side_str);
        double contracts = 0.0, average_opening_price = 0.0, unrealized_profit_loss = 0.0;
        (void)position_element["pos"].get_double_in_string().get(contracts);
        (void)position_element["avgPx"].get_double_in_string().get(average_opening_price);
        (void)position_element["upl"].get_double_in_string().get(unrealized_profit_loss);

        if (contracts > 0.0) {
            SwapPosition swap_position;
            swap_position.instrument = instrument;
            swap_position.position_side = position_side;
            swap_position.contracts = contracts;
            swap_position.average_opening_price = average_opening_price;
            swap_position.unrealized_profit_loss = unrealized_profit_loss;
            result[instrument][position_side] = swap_position;
            INFO("Query swap position: [INSTRUMENT] " + instrument + ", [POSITION_SIDE] " + ToString(position_side) +
                 ", [CONTRACTS] " + std::to_string(contracts) + ", [AVERAGE_OPENING_PRICE] " +
                 std::to_string(average_opening_price));
        }
    }
    return result;
}

void OkxClient::OnPublicMessage(const std::string& raw) {
    simdjson::padded_string padded(raw);
    simdjson::ondemand::document doc;
    if (public_parser_.iterate(padded).get(doc)) {
        return;
    }

    simdjson::ondemand::object arg_obj;
    if (doc["arg"].get_object().get(arg_obj)) {
        return;
    }

    std::string_view channel, inst_id;
    (void)arg_obj["channel"].get(channel);
    (void)arg_obj["instId"].get(inst_id);
    std::string inst_id_str(inst_id);

    simdjson::ondemand::array data_array;
    if (doc["data"].get_array().get(data_array)) {
        return;
    }

    if (channel == OkxChannelTickers) {
        for (auto element : data_array) {
            DecodeTicker(element.value(), inst_id_str);
            break;
        }
    } else if (channel == OkxChannelBBO) {
        for (auto element : data_array) {
            DecodeBBO(element.value(), inst_id_str);
            break;
        }
    } else if (channel == OkxChannelBooks5) {
        for (auto element : data_array) {
            DecodeDepth(element.value(), inst_id_str);
            break;
        }
    } else if (channel == OkxChannelTrades) {
        for (auto element : data_array) {
            DecodeTrade(element.value(), inst_id_str);
        }
    }
}

void OkxClient::OnPrivateMessage(const std::string& raw) {
    if (raw.find("\"event\"") != std::string::npos) {
        return;
    }

    simdjson::padded_string padded(raw);
    simdjson::ondemand::document doc;
    if (private_parser_.iterate(padded).get(doc)) {
        return;
    }

    if (raw.find("\"op\"") != std::string::npos) {
        std::string_view op_value, code_str, id_str;
        (void)doc["op"].get(op_value);
        (void)doc["code"].get(code_str);
        (void)doc["id"].get(id_str);
        std::string op(op_value);
        std::string code(code_str);
        std::string id(id_str);

        if (code != "0") {
            std::string_view msg_str;
            (void)doc["msg"].get(msg_str);
            std::string msg(msg_str);

            simdjson::ondemand::array data_array;
            if (!doc["data"].get_array().get(data_array)) {
                for (auto elem : data_array) {
                    std::string_view sub_msg;
                    if (!elem["sMsg"].get(sub_msg)) {
                        msg = std::string(sub_msg);
                    }
                    break;
                }
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
                    report.market_type = req.market_type;
                    report.position_side = req.position_side;
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

    simdjson::ondemand::object arg_obj;
    if (doc["arg"].get_object().get(arg_obj)) {
        return;
    }

    std::string_view channel;
    (void)arg_obj["channel"].get(channel);

    simdjson::ondemand::array data_array;
    if (doc["data"].get_array().get(data_array)) {
        return;
    }

    if (channel == OkxChannelOrders) {
        for (auto element : data_array) {
            DecodeOrderUpdate(element.value());
        }
    } else if (channel == OkxChannelAccount) {
        for (auto element : data_array) {
            DecodeAccountUpdate(element.value());
        }
    }
}

void OkxClient::DecodeOrderUpdate(simdjson::ondemand::value data) {
    if (!on_order_update_) {
        return;
    }

    std::string_view inst_id, order_id, state, side, pos_side, fee_currency;
    (void)data["instId"].get(inst_id);
    (void)data["ordId"].get(order_id);
    (void)data["state"].get(state);
    (void)data["side"].get(side);
    (void)data["posSide"].get(pos_side);
    (void)data["feeCcy"].get(fee_currency);

    std::string inst_id_str(inst_id);

    ExecutionReport report{};
    report.timestamp_ns = NowNs();
    report.SetInstrument(inst_id_str.c_str());
    report.SetOrderId(std::string(order_id).c_str());
    report.status = OkxParseOrderState(state);
    report.side = OkxParseSide(side.empty() ? "buy" : side);
    report.market_type = DetectMarketType(inst_id_str.c_str());
    report.position_side = OkxParsePosSide(pos_side.empty() ? "net" : pos_side);
    (void)data["px"].get_double_in_string().get(report.price);
    (void)data["accFillSz"].get_double_in_string().get(report.filled_volume);
    (void)data["sz"].get_double_in_string().get(report.total_volume);
    (void)data["avgPx"].get_double_in_string().get(report.avg_fill_price);
    (void)data["fillFee"].get_double_in_string().get(report.fee);
    report.SetFeeCurrency(std::string(fee_currency).c_str());
    on_order_update_(report);
}

void OkxClient::DecodeTicker(simdjson::ondemand::value data, const std::string& inst_id) {
    if (!on_ticker_) {
        return;
    }
    Ticker ticker{};
    (void)data["ts"].get_uint64_in_string().get(ticker.exchange_ts_ms);
    ticker.local_ts_ns = NowNs();
    ticker.SetInstrument(inst_id.c_str());
    ticker.market_type = DetectMarketType(inst_id.c_str());
    (void)data["last"].get_double_in_string().get(ticker.last_price);
    (void)data["lastSz"].get_double_in_string().get(ticker.last_volume);
    (void)data["bidPx"].get_double_in_string().get(ticker.bid_price);
    (void)data["bidSz"].get_double_in_string().get(ticker.bid_volume);
    (void)data["askPx"].get_double_in_string().get(ticker.ask_price);
    (void)data["askSz"].get_double_in_string().get(ticker.ask_volume);
    (void)data["open24h"].get_double_in_string().get(ticker.open_24h);
    (void)data["high24h"].get_double_in_string().get(ticker.high_24h);
    (void)data["low24h"].get_double_in_string().get(ticker.low_24h);
    (void)data["vol24h"].get_double_in_string().get(ticker.volume_24h);
    (void)data["volCcy24h"].get_double_in_string().get(ticker.volume_currency_24h);
    on_ticker_(ticker);
}

void OkxClient::DecodeBBO(simdjson::ondemand::value data, const std::string& inst_id) {
    if (!on_bbo_) {
        return;
    }
    BBO bbo{};
    (void)data["ts"].get_uint64_in_string().get(bbo.exchange_ts_ms);
    bbo.local_ts_ns = NowNs();
    bbo.SetInstrument(inst_id.c_str());
    bbo.market_type = DetectMarketType(inst_id.c_str());

    simdjson::ondemand::array bids_array;
    if (!data["bids"].get_array().get(bids_array)) {
        for (auto bid_level : bids_array) {
            simdjson::ondemand::array level_arr;
            if (bid_level.get_array().get(level_arr)) {
                break;
            }
            int idx = 0;
            for (auto elem : level_arr) {
                if (idx == 0) {
                    (void)elem.get_double_in_string().get(bbo.bid_price);
                } else if (idx == 1) {
                    (void)elem.get_double_in_string().get(bbo.bid_volume);
                }
                if (++idx >= 2) {
                    break;
                }
            }
            break;
        }
    }

    simdjson::ondemand::array asks_array;
    if (!data["asks"].get_array().get(asks_array)) {
        for (auto ask_level : asks_array) {
            simdjson::ondemand::array level_arr;
            if (ask_level.get_array().get(level_arr)) {
                break;
            }
            int idx = 0;
            for (auto elem : level_arr) {
                if (idx == 0) {
                    (void)elem.get_double_in_string().get(bbo.ask_price);
                } else if (idx == 1) {
                    (void)elem.get_double_in_string().get(bbo.ask_volume);
                }
                if (++idx >= 2) {
                    break;
                }
            }
            break;
        }
    }
    on_bbo_(bbo);
}

void OkxClient::DecodeDepth(simdjson::ondemand::value data, const std::string& inst_id) {
    if (!on_depth_) {
        return;
    }
    Depth depth{};
    (void)data["ts"].get_uint64_in_string().get(depth.exchange_ts_ms);
    depth.local_ts_ns = NowNs();
    depth.SetInstrument(inst_id.c_str());
    depth.market_type = DetectMarketType(inst_id.c_str());

    depth.ask_levels = 0;
    simdjson::ondemand::array asks_array;
    if (!data["asks"].get_array().get(asks_array)) {
        for (auto level_result : asks_array) {
            if (depth.ask_levels >= MAX_DEPTH_LEVELS) {
                break;
            }
            simdjson::ondemand::array level_arr;
            if (level_result.get_array().get(level_arr)) {
                continue;
            }
            int idx = 0;
            for (auto elem : level_arr) {
                if (idx == 0) {
                    (void)elem.get_double_in_string().get(depth.asks[depth.ask_levels].price);
                } else if (idx == 1) {
                    (void)elem.get_double_in_string().get(depth.asks[depth.ask_levels].volume);
                } else if (idx == 3) {
                    uint64_t count = 0;
                    (void)elem.get_uint64_in_string().get(count);
                    depth.asks[depth.ask_levels].order_count = static_cast<int>(count);
                }
                ++idx;
                if (idx > 3) {
                    break;
                }
            }
            ++depth.ask_levels;
        }
    }

    depth.bid_levels = 0;
    simdjson::ondemand::array bids_array;
    if (!data["bids"].get_array().get(bids_array)) {
        for (auto level_result : bids_array) {
            if (depth.bid_levels >= MAX_DEPTH_LEVELS) {
                break;
            }
            simdjson::ondemand::array level_arr;
            if (level_result.get_array().get(level_arr)) {
                continue;
            }
            int idx = 0;
            for (auto elem : level_arr) {
                if (idx == 0) {
                    (void)elem.get_double_in_string().get(depth.bids[depth.bid_levels].price);
                } else if (idx == 1) {
                    (void)elem.get_double_in_string().get(depth.bids[depth.bid_levels].volume);
                } else if (idx == 3) {
                    uint64_t count = 0;
                    (void)elem.get_uint64_in_string().get(count);
                    depth.bids[depth.bid_levels].order_count = static_cast<int>(count);
                }
                ++idx;
                if (idx > 3) {
                    break;
                }
            }
            ++depth.bid_levels;
        }
    }
    on_depth_(depth);
}

void OkxClient::DecodeTrade(simdjson::ondemand::value data, const std::string& inst_id) {
    if (!on_trade_) {
        return;
    }
    Trade trade{};
    (void)data["ts"].get_uint64_in_string().get(trade.exchange_ts_ms);
    trade.local_ts_ns = NowNs();
    trade.SetInstrument(inst_id.c_str());
    trade.market_type = DetectMarketType(inst_id.c_str());

    std::string_view trade_id, side;
    (void)data["tradeId"].get(trade_id);
    (void)data["px"].get_double_in_string().get(trade.price);
    (void)data["sz"].get_double_in_string().get(trade.volume);
    (void)data["side"].get(side);

    auto copy_len = std::min(trade_id.size(), sizeof(trade.trade_id) - 1);
    std::memcpy(trade.trade_id, trade_id.data(), copy_len);
    trade.trade_id[copy_len] = '\0';
    trade.side = OkxParseSide(side.empty() ? "buy" : side);
    on_trade_(trade);
}

void OkxClient::DecodeAccountUpdate(simdjson::ondemand::value data) {
    if (!on_balance_update_) {
        return;
    }
    simdjson::ondemand::array details_array;
    if (data["details"].get_array().get(details_array)) {
        return;
    }
    for (auto detail_result : details_array) {
        simdjson::ondemand::value detail;
        if (detail_result.get(detail)) {
            continue;
        }
        std::string_view currency;
        if (detail["ccy"].get(currency) || currency.empty()) {
            continue;
        }
        double available = 0.0, frozen = 0.0;
        (void)detail["availBal"].get_double_in_string().get(available);
        (void)detail["frozenBal"].get_double_in_string().get(frozen);
        on_balance_update_(std::string(currency), available, frozen);
    }
}
