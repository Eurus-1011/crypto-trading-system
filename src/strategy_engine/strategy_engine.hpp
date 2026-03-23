#pragma once

#include "common/config.hpp"
#include "common/cpu_affinity.hpp"
#include "common/logger.hpp"
#include "common/quotation.hpp"
#include "common/trading.hpp"
#include "common/utils.hpp"

#include <atomic>
#include <thread>

class StrategyEngine {
  public:
    StrategyEngine(const SystemConfig& config, TickerRing* ticker_ring, SignalRing* signal_ring);
    void Run();
    void Stop();

  private:
    const SystemConfig& config_;
    TickerRing* ticker_ring_;
    SignalRing* signal_ring_;
    std::atomic<bool> running_{true};
};
