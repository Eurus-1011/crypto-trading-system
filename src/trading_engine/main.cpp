#include "clients/okx/okx.hpp"
#include "common/config.hpp"
#include "common/logger.hpp"
#include "common/trading.hpp"

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

    InitLog(cfg.log_dir + "/trading_engine.log");
    LOG_INFO("trading_engine starting");

    SignalRing* signal_ring = nullptr;
    for (int retry = 0; retry < 30 && g_running; ++retry) {
        signal_ring = shm_attach<Signal, 1024>(cfg.shm.signal.c_str());
        if (signal_ring) {
            break;
        }
        LOG_WARN("waiting for signal shm...");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!signal_ring) {
        LOG_ERROR("failed to attach signal shm");
        return 1;
    }

    OkxClient client(cfg.exchange);
    client.LoginPrivate();
    LOG_INFO("private ws connected and logged in");

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    while (g_running) {
        Signal signal{};
        if (!shm_pop(signal_ring, signal)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        OrderRequest request;
        request.instrument = signal.instrument;
        request.side = signal.side;
        request.order_type = signal.order_type;
        request.size = std::to_string(signal.volume);

        std::string side_str = (request.side == Side::BUY) ? "buy" : "sell";
        LOG_INFO("executing: " + request.instrument + " " + side_str + " sz=" + request.size);

        OrderResult result = client.PlaceOrder(request);

        if (result.success) {
            LOG_INFO("order placed: ordId=" + result.order_id + " msg=" + result.message);
        } else {
            LOG_ERROR("order failed: code=" + result.code + " msg=" + result.message);
        }
    }

    LOG_INFO("trading_engine stopping");
    shm_detach(signal_ring);
    return 0;
}
