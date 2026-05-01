#pragma once

#include "clients/okx/okx.hpp"
#include "trading_engine/position_manager.hpp"

#include <memory>
#include <unordered_set>

class TradingEngine {
  public:
    TradingEngine(const SystemConfig& config, SignalRing* signal_ring, ExecutionReportRing* report_ring);
    void Init();
    void Run();
    void Stop();
    const std::vector<ExecutionReport>& GetPendingOrders() const { return pending_orders_; }
    PositionManager& GetPositionManager() { return position_manager_; }

  private:
    struct ReservationInfo {
        std::string instrument;
        std::string currency;
        double amount;
        Side side;
        TradeMode trade_mode;
    };

    void RunOrderDispatcher();
    void RunOrderListener();
    void RunReconciler();
    void ReconcileOrders();
    void ReconcileBalances();
    void ReconcileSwapPositions();
    void HandleOrderUpdate(const ExecutionReport& report, std::string_view client_order_id);

    const SystemConfig& config_;
    SignalRing* signal_ring_;
    ExecutionReportRing* report_ring_;
    std::unique_ptr<OkxClient> client_;
    PositionManager position_manager_;
    std::atomic<bool> running_{true};
    std::vector<ExecutionReport> pending_orders_;
    std::vector<ExecutionReport> all_pending_orders_;
    std::unordered_set<std::string> strategy_instruments_;
    std::unordered_map<std::string, std::string> live_order_instruments_;
    mutable std::mutex live_orders_mutex_;
    std::unordered_map<std::string, ReservationInfo> client_order_id_to_reservation_;
    mutable std::mutex client_order_id_mutex_;
};
