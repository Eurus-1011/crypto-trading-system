#include "strategy_engine.hpp"

#include "common/cpu_affinity.hpp"
#include "common/logger.hpp"

#include <chrono>
#include <immintrin.h>

StrategyEngine::StrategyEngine(const SystemConfig& config, TickerRing* ticker_ring, BBORing* bbo_ring,
                               DepthRing* depth_ring, TradeRing* trade_ring, SignalRing* signal_ring,
                               ExecutionReportRing* report_ring)
    : config_(config), ticker_ring_(ticker_ring), bbo_ring_(bbo_ring), depth_ring_(depth_ring), trade_ring_(trade_ring),
      signal_ring_(signal_ring), report_ring_(report_ring) {}

void StrategyEngine::Run() {
    if (!config_.strategy_engine.cpu_affinity.empty()) {
        BindThreadToCpus(config_.strategy_engine.cpu_affinity);
    }

    auto strategy = factory_ ? factory_() : nullptr;
    if (!strategy) {
        ERROR("Unknown strategy: [NAME] " + config_.strategy_engine.name);
        return;
    }

    strategy->Bind(signal_ring_);
    strategy->SetPositionManager(position_manager_);
    strategy->Init(config_.strategy_engine.params_json);

    if (!pending_orders_.empty()) {
        strategy->Reconstruct(pending_orders_);
    }

    INFO("Start strategy engine: [STRATEGY] " + config_.strategy_engine.name);

    auto last_timer_ts = std::chrono::steady_clock::now();
    static constexpr auto TIMER_INTERVAL = std::chrono::seconds(1);

    while (running_ && strategy->IsRunning()) {
        bool has_data = false;

        Ticker ticker{};
        if (shm_pop(ticker_ring_, ticker)) {
            strategy->OnTicker(ticker);
            has_data = true;
        }

        BBO bbo{};
        if (shm_pop(bbo_ring_, bbo)) {
            strategy->OnBBO(bbo);
            has_data = true;
        }

        Depth depth{};
        if (shm_pop(depth_ring_, depth)) {
            strategy->OnDepth(depth);
            has_data = true;
        }

        Trade trade{};
        if (shm_pop(trade_ring_, trade)) {
            strategy->OnTrade(trade);
            has_data = true;
        }

        ExecutionReport report{};
        if (shm_pop(report_ring_, report)) {
            strategy->OnExecutionReport(report);
            has_data = true;
        }

        auto now = std::chrono::steady_clock::now();
        if (now - last_timer_ts >= TIMER_INTERVAL) {
            strategy->OnTimer();
            last_timer_ts = now;
        }

        if (!has_data) {
            _mm_pause();
        }
    }

    INFO("Stop strategy engine");
}

void StrategyEngine::Stop() { running_ = false; }
