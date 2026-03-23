#include "strategy_engine.hpp"

#include "strategy_engine/mesh.hpp"

StrategyEngine::StrategyEngine(const SystemConfig& config, TickerRing* ticker_ring, BBORing* bbo_ring,
                               DepthRing* depth_ring, TradeRing* trade_ring, SignalRing* signal_ring,
                               ExecutionReportRing* report_ring)
    : config_(config), ticker_ring_(ticker_ring), bbo_ring_(bbo_ring), depth_ring_(depth_ring), trade_ring_(trade_ring),
      signal_ring_(signal_ring), report_ring_(report_ring) {}

std::unique_ptr<Strategy> StrategyEngine::CreateStrategy(const std::string& name) {
    if (name == "mesh") {
        return std::make_unique<MeshStrategy>();
    }
    return nullptr;
}

void StrategyEngine::Run() {
    if (!config_.strategy_engine.cpu_affinity.empty()) {
        BindThreadToCpus(config_.strategy_engine.cpu_affinity);
    }

    auto strategy = CreateStrategy(config_.strategy_engine.name);
    if (!strategy) {
        ERROR("Unknown strategy: [NAME] " + config_.strategy_engine.name);
        return;
    }

    strategy->Bind(signal_ring_);
    strategy->Init(config_.strategy_engine.params);

    if (!pending_orders_.empty()) {
        strategy->Reconstruct(pending_orders_);
    }

    INFO("Start strategy engine: [STRATEGY] " + config_.strategy_engine.name);

    uint64_t last_timer_ns = NowNs();
    static constexpr uint64_t TIMER_INTERVAL_NS = 1000000000ULL;

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

        uint64_t now = NowNs();
        if (now - last_timer_ns >= TIMER_INTERVAL_NS) {
            strategy->OnTimer();
            last_timer_ns = now;
        }

        if (!has_data) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    INFO("Stop strategy engine");
}

void StrategyEngine::Stop() { running_ = false; }
