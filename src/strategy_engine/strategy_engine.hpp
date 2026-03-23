#pragma once

#include "common/config.hpp"
#include "common/cpu_affinity.hpp"
#include "common/logger.hpp"
#include "common/quotation.hpp"
#include "common/trading.hpp"
#include "strategy_engine/strategy.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

class StrategyEngine {
  public:
    StrategyEngine(const SystemConfig& config, TickerRing* ticker_ring, BBORing* bbo_ring, DepthRing* depth_ring,
                   TradeRing* trade_ring, SignalRing* signal_ring, ExecutionReportRing* report_ring);
    void Run();
    void Stop();
    void SetPendingOrders(const std::vector<ExecutionReport>& orders) { pending_orders_ = orders; }

  private:
    std::unique_ptr<Strategy> CreateStrategy(const std::string& name);

    const SystemConfig& config_;
    TickerRing* ticker_ring_;
    BBORing* bbo_ring_;
    DepthRing* depth_ring_;
    TradeRing* trade_ring_;
    SignalRing* signal_ring_;
    ExecutionReportRing* report_ring_;
    std::atomic<bool> running_{true};
    std::vector<ExecutionReport> pending_orders_;
};
