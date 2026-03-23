#pragma once

#include "clients/okx/okx.hpp"
#include "common/config.hpp"
#include "common/cpu_affinity.hpp"
#include "common/logger.hpp"
#include "common/quotation.hpp"

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
    ExchangeClient* client_ = nullptr;
};
