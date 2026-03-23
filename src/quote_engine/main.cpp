#include "clients/okx/okx.hpp"
#include "common/config.hpp"
#include "common/logger.hpp"
#include "common/quotation.hpp"

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

    InitLog(cfg.log_dir + "/quote_engine.log");
    LOG_INFO("quote_engine starting");

    auto* ticker_ring = shm_create<Ticker, 4096>(cfg.shm.ticker.c_str());
    auto* bbo_ring = shm_create<BBO, 8192>(cfg.shm.bbo.c_str());
    auto* depth_ring = shm_create<Depth, 2048>(cfg.shm.depth.c_str());
    auto* trade_ring = shm_create<Trade, 8192>(cfg.shm.trade.c_str());

    OkxClient client(cfg.exchange);

    client.OnTicker([&](const Ticker& ticker) { shm_push(ticker_ring, ticker); });
    client.OnBBO([&](const BBO& bbo) { shm_push(bbo_ring, bbo); });
    client.OnDepth([&](const Depth& depth) { shm_push(depth_ring, depth); });
    client.OnTrade([&](const Trade& trade) { shm_push(trade_ring, trade); });

    for (auto& instrument : cfg.market.instruments) {
        for (auto& channel : cfg.market.channels) {
            client.Subscribe(channel, instrument);
            LOG_INFO("subscribed: " + channel + " " + instrument);
        }
    }

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    std::thread ws_thread([&]() { client.Start(); });

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    client.Stop();
    if (ws_thread.joinable()) {
        ws_thread.join();
    }

    LOG_INFO("quote_engine stopping");
    shm_destroy(cfg.shm.ticker.c_str(), ticker_ring);
    shm_destroy(cfg.shm.bbo.c_str(), bbo_ring);
    shm_destroy(cfg.shm.depth.c_str(), depth_ring);
    shm_destroy(cfg.shm.trade.c_str(), trade_ring);
    return 0;
}
