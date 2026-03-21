#pragma once

#include <cstdint>
#include <fstream>
#include <json/json.h>
#include <string>
#include <vector>

namespace cts {

struct ExchangeConfig {
    std::string name;
    std::string api_key;
    std::string secret_key;
    std::string passphrase;
    std::string base_url;
    bool simulated = true;
};

struct MarketConfig {
    std::vector<std::string> instruments;
    uint32_t poll_interval_ms = 200;
};

struct StrategyConfig {
    std::string name;
    std::string instrument;
    std::string side;
    std::string size;
};

struct ShmConfig {
    std::string market_data;
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
    std::ifstream f(path);
    if (!f.is_open()) {
        err = "cannot open config: " + path;
        return false;
    }
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string parse_err;
    if (!Json::parseFromStream(builder, f, &root, &parse_err)) {
        err = "invalid JSON: " + parse_err;
        return false;
    }

    auto& ex = root["exchange"];
    out.exchange.name = ex.get("name", "okx").asString();
    out.exchange.api_key = ex.get("api_key", "").asString();
    out.exchange.secret_key = ex.get("secret_key", "").asString();
    out.exchange.passphrase = ex.get("passphrase", "").asString();
    out.exchange.base_url = ex.get("base_url", "https://www.okx.com").asString();
    out.exchange.simulated = ex.get("simulated", true).asBool();

    auto& mk = root["market"];
    out.market.instruments.clear();
    if (mk.isMember("instruments") && mk["instruments"].isArray()) {
        for (Json::ArrayIndex i = 0; i < mk["instruments"].size(); ++i) {
            out.market.instruments.push_back(mk["instruments"][i].asString());
        }
    }
    out.market.poll_interval_ms = mk.get("poll_interval_ms", 200).asUInt();

    auto& st = root["strategy"];
    out.strategy.name = st.get("name", "buy_once").asString();
    out.strategy.instrument = st.get("instrument", "BTC-USDT").asString();
    out.strategy.side = st.get("side", "buy").asString();
    out.strategy.size = st.get("size", "10").asString();

    auto& sh = root["shm"];
    out.shm.market_data = sh.get("market_data", "/cts_market_data").asString();
    out.shm.signal = sh.get("signal", "/cts_signal").asString();

    out.log_dir = root.get("log_dir", "logs").asString();

    if (out.exchange.api_key.empty() || out.exchange.secret_key.empty()) {
        err = "exchange.api_key and exchange.secret_key are required";
        return false;
    }
    return true;
}

} // namespace cts
