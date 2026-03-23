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
};

class PositionManager {
  public:
    void InitFromExchange(const std::map<std::string, std::pair<double, double>>& balances);
    void SyncFromExchange(const std::string& currency, double exchange_available, double exchange_frozen);
    void UpdateOnNew(const ExecutionReport& report);
    void UpdateOnFill(const ExecutionReport& report);
    void UpdateOnCancel(const ExecutionReport& report);
    Position GetPosition(const std::string& currency) const;

  private:
    static std::string ExtractBaseCurrency(const char* instrument);
    static std::string ExtractQuoteCurrency(const char* instrument);

    mutable std::mutex mutex_;
    std::map<std::string, Position> positions_;
};
