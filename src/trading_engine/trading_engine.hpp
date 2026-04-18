#pragma once

#include "clients/okx/okx.hpp"
#include "trading_engine/position_manager.hpp"

#include <memory>

class TradingEngine {
  public:
    TradingEngine(const SystemConfig& config, SignalRing* signal_ring, ExecutionReportRing* report_ring);
    void Init();
    void Run();
    void Stop();
    const std::vector<ExecutionReport>& GetPendingOrders() const { return pending_orders_; }
    PositionManager& GetPositionManager() { return position_manager_; }

  private:
    void RunOrderDispatcher();
    void RunOrderListener();
    void RunReconciler();
    void ReconcileOrders();
    void ReconcileBalances();
    void ReconcileSwapPositions();
    void HandleOrderUpdate(const ExecutionReport& report);

    const SystemConfig& config_;
    SignalRing* signal_ring_;
    ExecutionReportRing* report_ring_;
    std::unique_ptr<OkxClient> client_;
    PositionManager position_manager_;
    std::atomic<bool> running_{true};
    std::vector<ExecutionReport> pending_orders_;
    std::unordered_map<std::string, std::string> live_order_instruments_;
    mutable std::mutex live_orders_mutex_;
};
