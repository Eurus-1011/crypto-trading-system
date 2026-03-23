#pragma once

#include "common/logger.hpp"
#include "common/trading.hpp"
#include "common/utils.hpp"

#include <cmath>
#include <map>
#include <mutex>
#include <string>

struct Position {
    std::string currency;
    double available = 0.0;
    double frozen = 0.0;
    uint64_t last_sync_ts_ns = 0;
};

class PositionManager {
  public:
    void UpdateOnFill(const ExecutionReport& report);
    void UpdateOnCancel(const ExecutionReport& report);
    void SyncFromExchange(const std::string& currency, double exchange_available);
    Position GetPosition(const std::string& currency) const;

  private:
    static std::string ExtractBaseCurrency(const char* instrument);

    mutable std::mutex mutex_;
    std::map<std::string, Position> positions_;
};
