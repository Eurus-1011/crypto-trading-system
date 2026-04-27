#pragma once

#include <simdjson.h>
#include <string>
#include <vector>

struct LoggerConfig {
    int cpu_affinity;
    std::string path;
};

struct ExchangeConfig {
    std::string name;
    std::string api_key;
    std::string secret_key;
    std::string passphrase;
};

struct QuotationEngineConfig {
    std::vector<std::string> instruments;
    std::vector<std::string> channels;
    std::vector<int> cpu_affinity;
};

struct StrategyEngineConfig {
    std::string name;
    std::string params_json;
    std::vector<int> cpu_affinity;
    std::vector<std::string> plugin_paths;
};

struct TradingEngineConfig {
    std::vector<int> cpu_affinity;
};

struct SystemConfig {
    LoggerConfig logger;
    ExchangeConfig exchange;
    QuotationEngineConfig quotation_engine;
    StrategyEngineConfig strategy_engine;
    TradingEngineConfig trading_engine;
};

inline void ParseCpuAffinity(simdjson::dom::element node, std::vector<int>& out) {
    out.clear();
    auto affinity_result = node["cpu_affinity"];
    if (affinity_result.error()) {
        return;
    }
    auto affinity = affinity_result.value();
    if (auto as_int = affinity.get_int64(); !as_int.error()) {
        out.push_back(static_cast<int>(as_int.value()));
    } else if (auto as_array = affinity.get_array(); !as_array.error()) {
        for (auto item : as_array.value()) {
            if (auto val = item.get_int64(); !val.error()) {
                out.push_back(static_cast<int>(val.value()));
            }
        }
    }
}

inline void ParseStringArray(simdjson::dom::element node, const char* key, std::vector<std::string>& out) {
    out.clear();
    auto array_result = node[key];
    if (array_result.error()) {
        return;
    }
    auto as_array = array_result.get_array();
    if (as_array.error()) {
        return;
    }
    for (auto item : as_array.value()) {
        if (auto as_string = item.get_string(); !as_string.error()) {
            out.emplace_back(as_string.value());
        }
    }
}

inline bool LoadConfig(const std::string& path, SystemConfig& out, std::string& err) {
    err.clear();
    simdjson::dom::parser parser;
    auto doc_result = parser.load(path);
    if (doc_result.error()) {
        err = "invalid JSON config: " + std::string(simdjson::error_message(doc_result.error()));
        return false;
    }
    auto root = doc_result.value();

    auto logger_result = root["logger"];
    if (!logger_result.error()) {
        auto logger = logger_result.value();
        int64_t cpu_val;
        if (!logger["cpu_affinity"].get(cpu_val)) {
            out.logger.cpu_affinity = static_cast<int>(cpu_val);
        }
        std::string_view path_val;
        if (!logger["path"].get(path_val)) {
            out.logger.path = std::string(path_val);
        }
    }

    auto exchange_result = root["exchange"];
    if (exchange_result.error()) {
        err = "missing 'exchange' section";
        return false;
    }
    auto exchange = exchange_result.value();
    std::string_view string_val;
    out.exchange.name = exchange["name"].get(string_val) ? "okx" : std::string(string_val);
    out.exchange.api_key = exchange["api_key"].get(string_val) ? "" : std::string(string_val);
    out.exchange.secret_key = exchange["secret_key"].get(string_val) ? "" : std::string(string_val);
    out.exchange.passphrase = exchange["passphrase"].get(string_val) ? "" : std::string(string_val);

    auto qe_result = root["quotation_engine"];
    if (!qe_result.error()) {
        auto qe = qe_result.value();
        ParseStringArray(qe, "instruments", out.quotation_engine.instruments);
        ParseStringArray(qe, "channels", out.quotation_engine.channels);
        ParseCpuAffinity(qe, out.quotation_engine.cpu_affinity);
    }

    auto se_result = root["strategy_engine"];
    if (!se_result.error()) {
        auto se = se_result.value();
        out.strategy_engine.name = se["name"].get(string_val) ? "mesh" : std::string(string_val);
        auto params_element = se["params"];
        if (!params_element.error()) {
            out.strategy_engine.params_json = simdjson::minify(params_element.value());
        } else {
            std::string_view params_path_sv;
            if (!se["params_path"].get(params_path_sv)) {
                simdjson::dom::parser params_parser;
                auto params_doc = params_parser.load(std::string(params_path_sv));
                if (params_doc.error()) {
                    err = "failed to load strategy_engine.params_path '" + std::string(params_path_sv) +
                          "': " + std::string(simdjson::error_message(params_doc.error()));
                    return false;
                }
                out.strategy_engine.params_json = simdjson::minify(params_doc.value());
            }
        }
        ParseCpuAffinity(se, out.strategy_engine.cpu_affinity);
        ParseStringArray(se, "plugin_paths", out.strategy_engine.plugin_paths);
    }

    auto te_result = root["trading_engine"];
    if (!te_result.error()) {
        ParseCpuAffinity(te_result.value(), out.trading_engine.cpu_affinity);
    }

    if (out.exchange.api_key.empty() || out.exchange.secret_key.empty()) {
        err = "exchange.api_key and exchange.secret_key are required";
        return false;
    }
    return true;
}
