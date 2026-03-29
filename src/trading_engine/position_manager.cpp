#include "position_manager.hpp"

#include "common/logger.hpp"

#include <cmath>

void PositionManager::InitSpotFromExchange(const std::map<std::string, std::pair<double, double>>& balances) {
    std::lock_guard<std::mutex> lock(mutex_);
    spot_positions_.clear();
    for (const auto& [currency, balance] : balances) {
        SpotPosition position;
        position.currency = currency;
        position.available = balance.first;
        position.frozen = balance.second;
        spot_positions_[currency] = position;
        INFO("Init spot position: [CURRENCY] " + currency + ", [AVAILABLE] " + std::to_string(position.available) +
             ", [TOTAL_FROZEN] " + std::to_string(position.frozen));
    }
}

void PositionManager::SyncSpotFromExchange(const std::string& currency, double exchange_available,
                                           double exchange_frozen) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& position = spot_positions_[currency];
    position.currency = currency;

    double available_diff = std::abs(position.available - exchange_available);
    double frozen_diff = std::abs(position.frozen - exchange_frozen);

    if (available_diff > 1e-8 || frozen_diff > 1e-8) {
        WARN("Spot position mismatch: [CURRENCY] " + currency + ", [LOCAL_AVAILABLE] " +
             std::to_string(position.available) + ", [EXCHANGE_AVAILABLE] " + std::to_string(exchange_available) +
             ", [LOCAL_FROZEN] " + std::to_string(position.frozen) + ", [EXCHANGE_FROZEN] " +
             std::to_string(exchange_frozen));
        position.available = exchange_available;
        position.frozen = exchange_frozen;
    }
}

void PositionManager::DeductSpot(const std::string& currency, double amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    spot_positions_[currency].available -= amount;
}

void PositionManager::RefundSpot(const std::string& currency, double amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    spot_positions_[currency].available += amount;
}

void PositionManager::UpdateSpotOnNew(const ExecutionReport& report) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (report.side == Side::BUY) {
        std::string quote_currency = ExtractCurrency<CurrencyPart::Quote>(report.instrument);
        auto& quote_position = spot_positions_[quote_currency];
        quote_position.currency = quote_currency;
        double freeze_amount = report.price * report.total_volume;
        quote_position.available -= freeze_amount;
        quote_position.frozen += freeze_amount;
        INFO("Freeze on new order: [CURRENCY] " + quote_currency + ", [FROZEN] " + std::to_string(freeze_amount) +
             ", [AVAILABLE] " + std::to_string(quote_position.available) + ", [TOTAL_FROZEN] " +
             std::to_string(quote_position.frozen));
    } else {
        std::string base_currency = ExtractCurrency<CurrencyPart::Base>(report.instrument);
        auto& base_position = spot_positions_[base_currency];
        base_position.currency = base_currency;
        double freeze_amount = report.total_volume;
        base_position.frozen += freeze_amount;
        INFO("Freeze on new order: [CURRENCY] " + base_currency + ", [FROZEN] " + std::to_string(freeze_amount) +
             ", [AVAILABLE] " + std::to_string(base_position.available) + ", [TOTAL_FROZEN] " +
             std::to_string(base_position.frozen));
    }
}

void PositionManager::UpdateSpotOnFill(const ExecutionReport& report) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string base_currency = ExtractCurrency<CurrencyPart::Base>(report.instrument);
    std::string quote_currency = ExtractCurrency<CurrencyPart::Quote>(report.instrument);

    if (report.status == OrderStatus::FILLED || report.status == OrderStatus::PARTIALLY_FILLED) {
        auto& base_position = spot_positions_[base_currency];
        auto& quote_position = spot_positions_[quote_currency];
        base_position.currency = base_currency;
        quote_position.currency = quote_currency;

        if (report.side == Side::BUY) {
            double quote_consumed = report.avg_fill_price * report.filled_volume;
            quote_position.frozen -= quote_consumed;
            base_position.available += report.filled_volume;
            if (std::string(report.fee_currency) == base_currency) {
                base_position.available += report.fee;
            } else if (std::string(report.fee_currency) == quote_currency) {
                quote_position.available += report.fee;
            }
        } else {
            double quote_received = report.avg_fill_price * report.filled_volume;
            base_position.frozen -= report.filled_volume;
            quote_position.available += quote_received;
            if (std::string(report.fee_currency) == quote_currency) {
                quote_position.available += report.fee;
            } else if (std::string(report.fee_currency) == base_currency) {
                base_position.available += report.fee;
            }
        }

        INFO("Update spot position on fill: [CURRENCY] " + base_currency + ", [SIDE] " + ToString(report.side) +
             ", [BASE_AVAILABLE] " + std::to_string(base_position.available) + ", [BASE_FROZEN] " +
             std::to_string(base_position.frozen) + ", [QUOTE_AVAILABLE] " + std::to_string(quote_position.available) +
             ", [QUOTE_FROZEN] " + std::to_string(quote_position.frozen));
    }
}

void PositionManager::UpdateSpotOnCancel(const ExecutionReport& report) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (report.status == OrderStatus::CANCELLED) {
        double remaining = report.total_volume - report.filled_volume;
        if (remaining > 1e-8) {
            if (report.side == Side::BUY) {
                std::string quote_currency = ExtractCurrency<CurrencyPart::Quote>(report.instrument);
                auto& quote_position = spot_positions_[quote_currency];
                quote_position.currency = quote_currency;
                double release_amount = remaining * report.price;
                quote_position.frozen -= release_amount;
                quote_position.available += release_amount;
                INFO("Release frozen on cancel: [CURRENCY] " + quote_currency + ", [RELEASED] " +
                     std::to_string(release_amount) + ", [AVAILABLE] " + std::to_string(quote_position.available) +
                     ", [TOTAL_FROZEN] " + std::to_string(quote_position.frozen));
            } else {
                std::string base_currency = ExtractCurrency<CurrencyPart::Base>(report.instrument);
                auto& base_position = spot_positions_[base_currency];
                base_position.currency = base_currency;
                base_position.frozen -= remaining;
                base_position.available += remaining;
                INFO("Release frozen on cancel: [CURRENCY] " + base_currency + ", [RELEASED] " +
                     std::to_string(remaining) + ", [AVAILABLE] " + std::to_string(base_position.available) +
                     ", [TOTAL_FROZEN] " + std::to_string(base_position.frozen));
            }
        }
        INFO("Order cancelled: [ORDER_ID] " + std::string(report.order_id) + ", [INSTRUMENT] " +
             std::string(report.instrument));
    }
}

void PositionManager::UpdateSpotOnRejected(const ExecutionReport& report) {
    if (report.side == Side::SELL) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string base_currency = ExtractCurrency<CurrencyPart::Base>(report.instrument);
        auto& base_position = spot_positions_[base_currency];
        base_position.available += report.total_volume;
        INFO("Refund on rejected sell: [CURRENCY] " + base_currency + ", [REFUND] " +
             std::to_string(report.total_volume) + ", [AVAILABLE] " + std::to_string(base_position.available));
    } else if (report.side == Side::BUY) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string quote_currency = ExtractCurrency<CurrencyPart::Quote>(report.instrument);
        auto& quote_position = spot_positions_[quote_currency];
        double release_amount = report.price * report.total_volume;
        if (quote_position.frozen >= release_amount - 1e-8) {
            quote_position.frozen -= release_amount;
            quote_position.available += release_amount;
            INFO("Release frozen on rejected buy: [CURRENCY] " + quote_currency + ", [RELEASED] " +
                 std::to_string(release_amount) + ", [AVAILABLE] " + std::to_string(quote_position.available) +
                 ", [TOTAL_FROZEN] " + std::to_string(quote_position.frozen));
        }
    }
}

SpotPosition PositionManager::GetSpotPosition(const std::string& currency) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spot_positions_.find(currency);
    if (it != spot_positions_.end()) {
        return it->second;
    }
    return SpotPosition{currency, 0.0, 0.0};
}

void PositionManager::InitSwapFromExchange(const std::map<std::string, std::map<PosSide, SwapPosition>>& positions) {
    std::lock_guard<std::mutex> lock(mutex_);
    swap_positions_.clear();
    for (const auto& [instrument, side_map] : positions) {
        for (const auto& [position_side, swap_position] : side_map) {
            swap_positions_[{instrument, position_side}] = swap_position;
            INFO("Init swap position: [INSTRUMENT] " + instrument + ", [POSITION_SIDE] " + ToString(position_side) +
                 ", [CONTRACTS] " + std::to_string(swap_position.contracts) + ", [AVERAGE_OPENING_PRICE] " +
                 std::to_string(swap_position.average_opening_price));
        }
    }
}

void PositionManager::SyncSwapFromExchange(const std::string& instrument, PosSide position_side, double contracts,
                                           double average_opening_price) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto key = std::make_pair(instrument, position_side);
    auto& position = swap_positions_[key];

    double contracts_diff = std::abs(position.contracts - contracts);
    if (contracts_diff > 1e-8) {
        WARN("Swap position mismatch: [INSTRUMENT] " + instrument + ", [POSITION_SIDE] " + ToString(position_side) +
             ", [LOCAL_CONTRACTS] " + std::to_string(position.contracts) + ", [EXCHANGE_CONTRACTS] " +
             std::to_string(contracts));
        position.instrument = instrument;
        position.position_side = position_side;
        position.contracts = contracts;
        position.average_opening_price = average_opening_price;
    }
}

void PositionManager::UpdateSwapOnFill(const ExecutionReport& report) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string instrument = report.instrument;
    auto key = std::make_pair(instrument, report.position_side);
    auto& position = swap_positions_[key];
    position.instrument = instrument;
    position.position_side = report.position_side;

    bool is_opening = false;
    switch (report.position_side) {
    case PosSide::NET:
        is_opening = (report.side == Side::BUY);
        break;
    case PosSide::LONG:
        is_opening = (report.side == Side::BUY);
        break;
    case PosSide::SHORT:
        is_opening = (report.side == Side::SELL);
        break;
    }

    if (is_opening) {
        position.contracts += report.filled_volume;
    } else {
        position.contracts -= report.filled_volume;
        if (position.contracts < 0.0) {
            position.contracts = 0.0;
        }
    }

    INFO("Update swap position on fill: [INSTRUMENT] " + instrument + ", [POSITION_SIDE] " +
         ToString(report.position_side) + ", [SIDE] " + ToString(report.side) + ", [FILLED_VOLUME] " +
         std::to_string(report.filled_volume) + ", [CONTRACTS] " + std::to_string(position.contracts));
}

void PositionManager::UpdateSwapOnCancel(const ExecutionReport& report) {
    INFO("Swap order cancelled: [ORDER_ID] " + std::string(report.order_id) + ", [INSTRUMENT] " +
         std::string(report.instrument));
}

void PositionManager::UpdateSwapOnRejected(const ExecutionReport& report) {
    ERROR("Swap order rejected: [ORDER_ID] " + std::string(report.order_id) + ", [INSTRUMENT] " +
          std::string(report.instrument));
}

SwapPosition PositionManager::GetSwapPosition(const std::string& instrument, PosSide position_side) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = swap_positions_.find({instrument, position_side});
    if (it != swap_positions_.end()) {
        return it->second;
    }
    return SwapPosition{instrument, position_side, 0.0, 0.0, 0.0};
}
