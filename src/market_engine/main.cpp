#include "client/okx/okx_client.hpp"
#include "common/config.hpp"
#include "common/logger.hpp"
#include "common/types.hpp"

#include <csignal>
#include <iostream>
#include <thread>

static volatile sig_atomic_t g_running = 1;
static void on_signal(int) { g_running = 0; }

int main(int argc, char* argv[]) {
    std::string config_path = (argc > 1) ? argv[1] : "config/config.json";

    cts::SystemConfig cfg;
    std::string err;
    if (!cts::LoadConfig(config_path, cfg, err)) {
        std::cerr << "config error: " << err << std::endl;
        return 1;
    }

    cts::InitLog(cfg.log_dir + "/market_engine.log");
    cts::LOG_INFO("market_engine starting");

    auto* ring = cts::shm_create<cts::MarketData, cts::MARKET_DATA_SHM_CAPACITY>(cfg.shm.market_data.c_str());
    if (!ring) {
        cts::LOG_ERROR("failed to create market_data shm: " + cfg.shm.market_data);
        return 1;
    }
    cts::LOG_INFO("shm created: " + cfg.shm.market_data);

    cts::OkxClient client(cfg.exchange.api_key, cfg.exchange.secret_key, cfg.exchange.passphrase,
                          cfg.exchange.base_url, cfg.exchange.simulated);

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    while (g_running) {
        for (auto& inst : cfg.market.instruments) {
            Json::Value resp = client.get_ticker(inst);

            if (resp["code"].asString() != "0" || !resp["data"].isArray() || resp["data"].empty()) {
                cts::LOG_WARN("ticker request failed for " + inst + ": " + resp.toStyledString());
                continue;
            }

            auto& tick = resp["data"][0];
            cts::MarketData md{};
            md.exchange_ts_ms = std::stoull(tick.get("ts", "0").asString());
            md.local_ts_ns = cts::now_ns();
            md.set_instrument(inst.c_str());
            md.last_price = std::stod(tick.get("last", "0").asString());
            md.best_bid = std::stod(tick.get("bidPx", "0").asString());
            md.best_ask = std::stod(tick.get("askPx", "0").asString());
            md.bid_size = std::stod(tick.get("bidSz", "0").asString());
            md.ask_size = std::stod(tick.get("askSz", "0").asString());
            md.volume_24h = std::stod(tick.get("vol24h", "0").asString());

            if (!cts::shm_push(ring, md)) {
                cts::LOG_WARN("shm ring full, dropping tick for " + inst);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.market.poll_interval_ms));
    }

    cts::LOG_INFO("market_engine stopping");
    cts::shm_destroy(cfg.shm.market_data.c_str(), ring);
    return 0;
}
