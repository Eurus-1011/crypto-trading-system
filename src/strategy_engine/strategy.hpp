#pragma once

#include "common/defs.hpp"
#include "common/utils.hpp"
#include "trading_engine/position_manager.hpp"

class Strategy {
  public:
    virtual ~Strategy() = default;

    void Bind(SignalRing* signal_ring) { signal_ring_ = signal_ring; }
    void SetPositionManager(PositionManager* position_manager) { position_manager_ = position_manager; }

    virtual void Init(const Json::Value& params) = 0;
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
    void EmitBuy(const char* instrument, OrderType order_type, double price, double volume,
                 MarketType market_type = MarketType::SPOT, PosSide position_side = PosSide::NET);

    void EmitSell(const char* instrument, OrderType order_type, double price, double volume,
                  MarketType market_type = MarketType::SPOT, PosSide position_side = PosSide::NET);

    void EmitCancel(const char* instrument, const char* order_id, MarketType market_type = MarketType::SPOT);

    SignalRing* signal_ring_ = nullptr;
    PositionManager* position_manager_ = nullptr;
    std::atomic<bool> running_{true};
};
