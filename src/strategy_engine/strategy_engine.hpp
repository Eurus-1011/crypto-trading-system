#pragma once

#include "common/config.hpp"
#include "common/cpu_affinity.hpp"
#include "common/logger.hpp"
#include "common/quotation.hpp"
#include "common/trading.hpp"
#include "common/utils.hpp"

#include <atomic>
#include <thread>

class StrategyEngine {
  public:
    StrategyEngine(const SystemConfig& config, TickerRing* ticker_ring, SignalRing* signal_ring)
        : config_(config), ticker_ring_(ticker_ring), signal_ring_(signal_ring) {}

    void Run() {
        if (!config_.strategy_engine.cpu_affinity.empty()) {
            BindThreadToCpus(config_.strategy_engine.cpu_affinity);
        }

        INFO("Start strategy engine: [STRATEGY] " + config_.strategy_engine.name + ", [INSTRUMENT] " +
             config_.strategy_engine.instrument);

        bool signal_sent = false;
        const std::string& target_instrument = config_.strategy_engine.instrument;

        while (running_) {
            Ticker ticker{};
            if (!shm_pop(ticker_ring_, ticker)) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }

            INFO("Receive ticker: [INSTRUMENT] " + std::string(ticker.instrument) + ", [LAST] " +
                 std::to_string(ticker.last_price) + ", [BID] " + std::to_string(ticker.bid_price) + ", [ASK] " +
                 std::to_string(ticker.ask_price));

            if (signal_sent) {
                continue;
            }
            if (std::string(ticker.instrument) != target_instrument) {
                continue;
            }

            Signal signal{};
            signal.timestamp_ns = NowNs();
            signal.SetInstrument(target_instrument.c_str());
            signal.side = (config_.strategy_engine.side == "sell") ? Side::SELL : Side::BUY;
            signal.order_type = OrderType::MARKET;
            signal.price = ticker.last_price;
            signal.volume = std::stod(config_.strategy_engine.size);

            if (shm_push(signal_ring_, signal)) {
                INFO("Emit signal success: [INSTRUMENT] " + target_instrument + ", [SIDE] " +
                     config_.strategy_engine.side + ", [SIZE] " + config_.strategy_engine.size);
                signal_sent = true;
            }
        }

        INFO("Stop strategy engine");
    }

    void Stop() { running_ = false; }

  private:
    const SystemConfig& config_;
    TickerRing* ticker_ring_;
    SignalRing* signal_ring_;
    std::atomic<bool> running_{true};
};
