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

struct ShmConfig {
    std::string ticker;
    std::string bbo;
    std::string depth;
    std::string trade;
    std::string signal;
};

struct QuotationEngineConfig {
    std::vector<std::string> instruments;
    std::vector<std::string> channels;
    std::vector<int> cpu_affinity;
};

struct StrategyEngineConfig {
    std::string name;
    std::string instrument;
    std::string side;
    std::string size;
    std::vector<int> cpu_affinity;
};

struct TradingEngineConfig {
    std::vector<int> cpu_affinity;
};

struct SystemConfig {
    ExchangeConfig exchange;
    ShmConfig shm;
    std::string log_dir;
    QuotationEngineConfig quotation_engine;
    StrategyEngineConfig strategy_engine;
    TradingEngineConfig trading_engine;
};

inline void ParseCpuAffinity(const Json::Value& node, std::vector<int>& out) {
    out.clear();
    if (!node.isMember("cpu_affinity")) {
        return;
    }
    auto& affinity = node["cpu_affinity"];
    if (affinity.isInt()) {
        out.push_back(affinity.asInt());
    } else if (affinity.isArray()) {
        for (Json::ArrayIndex idx = 0; idx < affinity.size(); ++idx) {
            out.push_back(affinity[idx].asInt());
        }
    }
}

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
    out.exchange.ws_public_url = exchange.get("ws_public_url", "wss://ws.okx.com/ws/v5/public").asString();
    out.exchange.ws_private_url = exchange.get("ws_private_url", "wss://ws.okx.com/ws/v5/private").asString();

    auto& shm = root["shm"];
    out.shm.ticker = shm.get("ticker", "/cts_ticker").asString();
    out.shm.bbo = shm.get("bbo", "/cts_bbo").asString();
    out.shm.depth = shm.get("depth", "/cts_depth").asString();
    out.shm.trade = shm.get("trade", "/cts_trade").asString();
    out.shm.signal = shm.get("signal", "/cts_signal").asString();

    out.log_dir = root.get("log_dir", "logs").asString();

    auto& qe = root["quotation_engine"];
    out.quotation_engine.instruments.clear();
    if (qe.isMember("instruments") && qe["instruments"].isArray()) {
        for (Json::ArrayIndex idx = 0; idx < qe["instruments"].size(); ++idx) {
            out.quotation_engine.instruments.push_back(qe["instruments"][idx].asString());
        }
    }
    out.quotation_engine.channels.clear();
    if (qe.isMember("channels") && qe["channels"].isArray()) {
        for (Json::ArrayIndex idx = 0; idx < qe["channels"].size(); ++idx) {
            out.quotation_engine.channels.push_back(qe["channels"][idx].asString());
        }
    }
    ParseCpuAffinity(qe, out.quotation_engine.cpu_affinity);

    auto& se = root["strategy_engine"];
    out.strategy_engine.name = se.get("name", "buy_once").asString();
    out.strategy_engine.instrument = se.get("instrument", "BTC-USDT").asString();
    out.strategy_engine.side = se.get("side", "buy").asString();
    out.strategy_engine.size = se.get("size", "10").asString();
    ParseCpuAffinity(se, out.strategy_engine.cpu_affinity);

    auto& te = root["trading_engine"];
    ParseCpuAffinity(te, out.trading_engine.cpu_affinity);

    if (out.exchange.api_key.empty() || out.exchange.secret_key.empty()) {
        err = "exchange.api_key and exchange.secret_key are required";
        return false;
    }
    return true;
}
