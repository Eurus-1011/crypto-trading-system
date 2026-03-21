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

    cts::InitLog(cfg.log_dir + "/strategy_engine.log");
    cts::LOG_INFO("strategy_engine starting");

    cts::MarketDataRing* md_ring = nullptr;
    for (int retry = 0; retry < 30 && g_running; ++retry) {
        md_ring = cts::shm_attach<cts::MarketData, cts::MARKET_DATA_SHM_CAPACITY>(cfg.shm.market_data.c_str());
        if (md_ring) break;
        cts::LOG_WARN("waiting for market_data shm...");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!md_ring) {
        cts::LOG_ERROR("failed to attach market_data shm");
        return 1;
    }

    auto* sig_ring = cts::shm_create<cts::Signal, cts::SIGNAL_SHM_CAPACITY>(cfg.shm.signal.c_str());
    if (!sig_ring) {
        cts::LOG_ERROR("failed to create signal shm: " + cfg.shm.signal);
        return 1;
    }
    cts::LOG_INFO("shm attached: " + cfg.shm.market_data + ", created: " + cfg.shm.signal);

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    bool signal_sent = false;
    const std::string& target_inst = cfg.strategy.instrument;

    while (g_running) {
        cts::MarketData md{};
        if (!cts::shm_pop(md_ring, md)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        cts::LOG_INFO("tick: " + std::string(md.instrument) + " last=" + std::to_string(md.last_price) +
                      " bid=" + std::to_string(md.best_bid) + " ask=" + std::to_string(md.best_ask));

        if (signal_sent) continue;
        if (std::string(md.instrument) != target_inst) continue;

        cts::Signal sig{};
        sig.timestamp_ns = cts::now_ns();
        sig.set_instrument(target_inst.c_str());
        sig.side = (cfg.strategy.side == "sell") ? cts::Side::SELL : cts::Side::BUY;
        sig.order_type = cts::OrderType::MARKET;
        sig.price = md.last_price;
        sig.quantity = std::stod(cfg.strategy.size);

        if (cts::shm_push(sig_ring, sig)) {
            cts::LOG_INFO("signal emitted: " + target_inst + " " + cfg.strategy.side + " " + cfg.strategy.size);
            signal_sent = true;
        }
    }

    cts::LOG_INFO("strategy_engine stopping");
    cts::shm_detach(md_ring);
    cts::shm_destroy(cfg.shm.signal.c_str(), sig_ring);
    return 0;
}
