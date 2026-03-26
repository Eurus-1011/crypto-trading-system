#pragma once

#include "common/logger.hpp"
#include "common/trading.hpp"

#include <cmath>
#include <map>

struct Position {
    std::string currency;
    double available = 0.0;
    double frozen = 0.0;
};

class PositionManager {
  public:
    void InitFromExchange(const std::map<std::string, std::pair<double, double>>& balances);
    void SyncFromExchange(const std::string& currency, double exchange_available, double exchange_frozen);
    void Deduct(const std::string& currency, double amount);
    void Refund(const std::string& currency, double amount);
    void UpdateOnNew(const ExecutionReport& report);
    void UpdateOnFill(const ExecutionReport& report);
    void UpdateOnCancel(const ExecutionReport& report);
    void UpdateOnRejected(const ExecutionReport& report);
    Position GetPosition(const std::string& currency) const;

  private:
    enum class CurrencyPart { Base, Quote };

    template <CurrencyPart Part>
    static std::string ExtractCurrency(const char* instrument) {
        const char* dash = std::strchr(instrument, '-');
        if (!dash) {
            return instrument;
        }
        if constexpr (Part == CurrencyPart::Base) {
            return std::string(instrument, dash);
        } else {
            return std::string(dash + 1);
        }
    }

    mutable std::mutex mutex_;
    std::map<std::string, Position> positions_;
};
