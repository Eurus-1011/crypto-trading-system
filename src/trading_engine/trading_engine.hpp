#pragma once

#include "clients/okx/okx.hpp"
#include "common/config.hpp"
#include "common/cpu_affinity.hpp"
#include "common/logger.hpp"
#include "common/trading.hpp"

#include <atomic>
#include <thread>

class TradingEngine {
  public:
    TradingEngine(const SystemConfig& config, SignalRing* signal_ring) : config_(config), signal_ring_(signal_ring) {}

    void Run() {
        if (!config_.trading_engine.cpu_affinity.empty()) {
            BindThreadToCpus(config_.trading_engine.cpu_affinity);
        }

        INFO("Start trading engine");

        OkxClient client(config_.exchange);
        client.LoginPrivate();
        INFO("Login private ws success");

        while (running_) {
            Signal signal{};
            if (!shm_pop(signal_ring_, signal)) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }

            OrderRequest request;
            request.instrument = signal.instrument;
            request.side = signal.side;
            request.order_type = signal.order_type;
            request.size = std::to_string(signal.volume);

            std::string side_str = (request.side == Side::BUY) ? "buy" : "sell";
            INFO("Execute order: [INSTRUMENT] " + request.instrument + ", [SIDE] " + side_str + ", [SIZE] " +
                 request.size);

            OrderResult result = client.PlaceOrder(request);

            if (result.success) {
                INFO("Place order success: [ORDER_ID] " + result.order_id + ", [MSG] " + result.message);
            } else {
                ERROR("Place order failed: [CODE] " + result.code + ", [MSG] " + result.message);
            }
        }

        INFO("Stop trading engine");
    }

    void Stop() { running_ = false; }

  private:
    const SystemConfig& config_;
    SignalRing* signal_ring_;
    std::atomic<bool> running_{true};
};
