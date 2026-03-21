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

    cts::InitLog(cfg.log_dir + "/trading_engine.log");
    cts::LOG_INFO("trading_engine starting");

    cts::SignalRing* sig_ring = nullptr;
    for (int retry = 0; retry < 30 && g_running; ++retry) {
        sig_ring = cts::shm_attach<cts::Signal, cts::SIGNAL_SHM_CAPACITY>(cfg.shm.signal.c_str());
        if (sig_ring) break;
        cts::LOG_WARN("waiting for signal shm...");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!sig_ring) {
        cts::LOG_ERROR("failed to attach signal shm");
        return 1;
    }
    cts::LOG_INFO("shm attached: " + cfg.shm.signal);

    cts::OkxClient client(cfg.exchange.api_key, cfg.exchange.secret_key, cfg.exchange.passphrase,
                          cfg.exchange.base_url, cfg.exchange.simulated);

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    while (g_running) {
        cts::Signal sig{};
        if (!cts::shm_pop(sig_ring, sig)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        std::string side = (sig.side == cts::Side::BUY) ? "buy" : "sell";
        std::string ord_type = (sig.order_type == cts::OrderType::MARKET) ? "market" : "limit";
        std::string size = std::to_string(sig.quantity);

        cts::LOG_INFO("executing: " + std::string(sig.instrument) + " " + side + " " + ord_type + " sz=" + size);

        Json::Value resp = client.place_order(sig.instrument, side, ord_type, size);

        std::string code = resp.get("code", "-1").asString();
        if (code == "0") {
            auto& data = resp["data"][0];
            cts::LOG_INFO("order placed: ordId=" + data.get("ordId", "?").asString() +
                          " sCode=" + data.get("sCode", "?").asString() +
                          " sMsg=" + data.get("sMsg", "").asString());
        } else {
            cts::LOG_ERROR("order failed: code=" + code + " msg=" + resp.get("msg", "unknown").asString());
        }
    }

    cts::LOG_INFO("trading_engine stopping");
    cts::shm_detach(sig_ring);
    return 0;
}
