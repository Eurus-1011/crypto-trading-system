#pragma once

#include "common/config.hpp"
#include "common/defs.hpp"

class ExchangeClient;

class QuotationEngine {
  public:
    QuotationEngine(const SystemConfig& config, TickerRing* ticker_ring, BBORing* bbo_ring, DepthRing* depth_ring,
                    TradeRing* trade_ring);
    void Run();
    void Stop();

  private:
    const SystemConfig& config_;
    TickerRing* ticker_ring_;
    BBORing* bbo_ring_;
    DepthRing* depth_ring_;
    TradeRing* trade_ring_;
    ExchangeClient* client_;
};
