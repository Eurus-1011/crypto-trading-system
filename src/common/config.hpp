#pragma once

#include <cstdint>
#include <fstream>
#include <json/json.h>
#include <string>
#include <vector>

struct ExchangeConfig {
    std::string name;
    std::string api_key;
    std::string secret_key;
    std::string passphrase;
    std::string base_url;
    std::string ws_public_url;
    std::string ws_private_url;
};

struct MarketConfig {
    std::vector<std::string> instruments;
    std::vector<std::string> channels;
};

struct StrategyConfig {
    std::string name;
    std::string instrument;
    std::string side;
    std::string size;
};

struct ShmConfig {
    std::string ticker;
    std::string bbo;
    std::string depth;
    std::string trade;
    std::string signal;
};

struct SystemConfig {
    ExchangeConfig exchange;
    MarketConfig market;
    StrategyConfig strategy;
    ShmConfig shm;
    std::string log_dir;
};

inline bool LoadConfig(const std::string& path, SystemConfig& out, std::string& err) {
    err.clear();
    std::ifstream file(path);
    if (!file.is_open()) {
        err = "cannot open config: " + path;
        return false;
    }
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string parse_err;
    if (!Json::parseFromStream(builder, file, &root, &parse_err)) {
        err = "invalid JSON: " + parse_err;
        return false;
    }

    auto& exchange = root["exchange"];
    out.exchange.name = exchange.get("name", "okx").asString();
    out.exchange.api_key = exchange.get("api_key", "").asString();
    out.exchange.secret_key = exchange.get("secret_key", "").asString();
    out.exchange.passphrase = exchange.get("passphrase", "").asString();
    out.exchange.base_url = exchange.get("base_url", "https://www.okx.com").asString();
    out.exchange.ws_public_url = exchange.get("ws_public_url", "wss://ws.okx.com:8443/ws/v5/public").asString();
    out.exchange.ws_private_url = exchange.get("ws_private_url", "wss://ws.okx.com:8443/ws/v5/private").asString();

    auto& market = root["market"];
    out.market.instruments.clear();
    if (market.isMember("instruments") && market["instruments"].isArray()) {
        for (Json::ArrayIndex idx = 0; idx < market["instruments"].size(); ++idx) {
            out.market.instruments.push_back(market["instruments"][idx].asString());
        }
    }
    out.market.channels.clear();
    if (market.isMember("channels") && market["channels"].isArray()) {
        for (Json::ArrayIndex idx = 0; idx < market["channels"].size(); ++idx) {
            out.market.channels.push_back(market["channels"][idx].asString());
        }
    }

    auto& strategy = root["strategy"];
    out.strategy.name = strategy.get("name", "buy_once").asString();
    out.strategy.instrument = strategy.get("instrument", "BTC-USDT").asString();
    out.strategy.side = strategy.get("side", "buy").asString();
    out.strategy.size = strategy.get("size", "10").asString();

    auto& shm = root["shm"];
    out.shm.ticker = shm.get("ticker", "/cts_ticker").asString();
    out.shm.bbo = shm.get("bbo", "/cts_bbo").asString();
    out.shm.depth = shm.get("depth", "/cts_depth").asString();
    out.shm.trade = shm.get("trade", "/cts_trade").asString();
    out.shm.signal = shm.get("signal", "/cts_signal").asString();

    out.log_dir = root.get("log_dir", "logs").asString();

    if (out.exchange.api_key.empty() || out.exchange.secret_key.empty()) {
        err = "exchange.api_key and exchange.secret_key are required";
        return false;
    }
    return true;
}
