#include "common/config.hpp"
#include "common/logger.hpp"
#include "common/quotation.hpp"
#include "common/trading.hpp"
#include "common/utils.hpp"

#include <csignal>
#include <iostream>
#include <thread>

static volatile sig_atomic_t g_running = 1;
static void OnSignal(int) { g_running = 0; }

int main(int argc, char* argv[]) {
    std::string config_path = (argc > 1) ? argv[1] : "config/config.json";

    SystemConfig cfg;
    std::string err;
    if (!LoadConfig(config_path, cfg, err)) {
        std::cerr << "config error: " << err << std::endl;
        return 1;
    }

    InitLog(cfg.log_dir + "/strategy_engine.log");
    LOG_INFO("strategy_engine starting");

    TickerRing* ticker_ring = nullptr;
    for (int retry = 0; retry < 30 && g_running; ++retry) {
        ticker_ring = shm_attach<Ticker, 4096>(cfg.shm.ticker.c_str());
        if (ticker_ring) {
            break;
        }
        LOG_WARN("waiting for ticker shm...");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!ticker_ring) {
        LOG_ERROR("failed to attach ticker shm");
        return 1;
    }

    auto* signal_ring = shm_create<Signal, 1024>(cfg.shm.signal.c_str());
    if (!signal_ring) {
        LOG_ERROR("failed to create signal shm");
        return 1;
    }

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    bool signal_sent = false;
    const std::string& target_instrument = cfg.strategy.instrument;

    while (g_running) {
        Ticker ticker{};
        if (!shm_pop(ticker_ring, ticker)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        LOG_INFO("tick: " + std::string(ticker.instrument) +
                 " last=" + std::to_string(ticker.last_price) +
                 " bid=" + std::to_string(ticker.bid_price) +
                 " ask=" + std::to_string(ticker.ask_price));

        if (signal_sent) {
            continue;
        }
        if (std::string(ticker.instrument) != target_instrument) {
            continue;
        }

        Signal signal{};
        signal.timestamp_ns = NowNs();
        signal.SetInstrument(target_instrument.c_str());
        signal.side = (cfg.strategy.side == "sell") ? Side::SELL : Side::BUY;
        signal.order_type = OrderType::MARKET;
        signal.price = ticker.last_price;
        signal.volume = std::stod(cfg.strategy.size);

        if (shm_push(signal_ring, signal)) {
            LOG_INFO("signal emitted: " + target_instrument + " " + cfg.strategy.side + " " + cfg.strategy.size);
            signal_sent = true;
        }
    }

    LOG_INFO("strategy_engine stopping");
    shm_detach(ticker_ring);
    shm_destroy(cfg.shm.signal.c_str(), signal_ring);
    return 0;
}
