#pragma once

#include "clients/okx/okx.hpp"
#include "common/config.hpp"
#include "common/cpu_affinity.hpp"
#include "common/logger.hpp"
#include "common/trading.hpp"
#include "trading_engine/position_manager.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

class TradingEngine {
  public:
    TradingEngine(const SystemConfig& config, SignalRing* signal_ring, ExecutionReportRing* report_ring);
    void Init();
    void Run();
    void Stop();
    const std::vector<ExecutionReport>& GetPendingOrders() const { return pending_orders_; }

  private:
    void RunOrderDispatcher();
    void RunOrderListener();
    void HandleOrderUpdate(const ExecutionReport& report);

    const SystemConfig& config_;
    SignalRing* signal_ring_;
    ExecutionReportRing* report_ring_;
    std::unique_ptr<OkxClient> client_;
    PositionManager position_manager_;
    std::atomic<bool> running_{true};
    std::vector<ExecutionReport> pending_orders_;
};
