#pragma once

#include "common/defs.hpp"
#include "trading_engine/position_manager.hpp"

#include <string_view>
#include <vector>

class Strategy {
  public:
    virtual ~Strategy() = default;

    void Bind(SignalRing* signal_ring) { signal_ring_ = signal_ring; }
    void SetPositionManager(PositionManager* position_manager) { position_manager_ = position_manager; }

    virtual void Init(std::string_view params_json) = 0;
    virtual void Reconstruct(const std::vector<ExecutionReport>& pending_orders) {}
    virtual void OnTicker(const Ticker& ticker) {}
    virtual void OnBBO(const BBO& bbo) {}
    virtual void OnDepth(const Depth& depth) {}
    virtual void OnTrade(const Trade& trade) {}
    virtual void OnExecutionReport(const ExecutionReport& report) = 0;
    virtual void OnTimer() = 0;

    bool IsRunning() const { return running_; }
    void Stop() { running_ = false; }

  protected:
    void EmitBuy(const char* instrument, OrderType order_type, Price price, Volume volume, MarketType market_type,
                 PosSide position_side);

    void EmitSell(const char* instrument, OrderType order_type, Price price, Volume volume, MarketType market_type,
                  PosSide position_side);

    void EmitCancel(const char* instrument, const char* order_id, MarketType market_type);

    SignalRing* signal_ring_ = nullptr;
    PositionManager* position_manager_ = nullptr;
    std::atomic<bool> running_{true};
};
