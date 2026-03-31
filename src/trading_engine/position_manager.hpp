#pragma once

#include "common/defs.hpp"

#include <map>
#include <mutex>
#include <unordered_map>

struct SpotPosition {
    std::string currency;
    double available = 0.0;
    double frozen = 0.0;
};

class PositionManager {
  public:
    void InitSpotFromExchange(const std::map<std::string, std::pair<double, double>>& balances);
    void SyncSpotFromExchange(const std::string& currency, double exchange_available, double exchange_frozen);
    void DeductSpot(const std::string& currency, double amount);
    void RefundSpot(const std::string& currency, double amount);
    void UpdateSpotOnNew(const ExecutionReport& report);
    void UpdateSpotOnFill(const ExecutionReport& report);
    void UpdateSpotOnCancel(const ExecutionReport& report);
    void UpdateSpotOnRejected(const ExecutionReport& report);
    SpotPosition GetSpotPosition(const std::string& currency) const;

    void InitSwapFromExchange(const std::map<std::string, std::map<PosSide, SwapPosition>>& positions);
    void SyncSwapFromExchange(const std::string& instrument, PosSide position_side, double contracts,
                              double average_opening_price);
    void UpdateSwapOnFill(const ExecutionReport& report);
    void UpdateSwapOnCancel(const ExecutionReport& report);
    void UpdateSwapOnRejected(const ExecutionReport& report);
    SwapPosition GetSwapPosition(const std::string& instrument, PosSide position_side) const;

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
    std::map<std::string, SpotPosition> spot_positions_;
    std::map<std::pair<std::string, PosSide>, SwapPosition> swap_positions_;
    std::unordered_map<std::string, double> spot_order_fill_tracker_;
    std::unordered_map<std::string, double> swap_order_fill_tracker_;
};
