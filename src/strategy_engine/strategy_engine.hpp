#pragma once

#include "common/config.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <position_manager.hpp>
#include <strategy.hpp>
#include <vector>

class StrategyEngine {
  public:
    using StrategyFactory = std::function<std::unique_ptr<Strategy>()>;

    StrategyEngine(const SystemConfig& config, TickerRing* ticker_ring, BBORing* bbo_ring, DepthRing* depth_ring,
                   TradeRing* trade_ring, SignalRing* signal_ring, ExecutionReportRing* report_ring);
    void Run();
    void Stop();
    void SetFactory(StrategyFactory factory) { factory_ = std::move(factory); }
    void SetPendingOrders(const std::vector<ExecutionReport>& orders) { pending_orders_ = orders; }
    void SetPositionManager(IPositionManager* pm) { position_manager_ = pm; }

  private:
    const SystemConfig& config_;
    TickerRing* ticker_ring_;
    BBORing* bbo_ring_;
    DepthRing* depth_ring_;
    TradeRing* trade_ring_;
    SignalRing* signal_ring_;
    ExecutionReportRing* report_ring_;
    IPositionManager* position_manager_;
    StrategyFactory factory_;
    std::atomic<bool> running_{true};
    std::vector<ExecutionReport> pending_orders_;
};
