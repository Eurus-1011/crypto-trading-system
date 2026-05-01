#include "position_manager.hpp"

#include "common/logger.hpp"

#include <cmath>
#include <utils.hpp>

void PositionManager::InitSpotFromExchange(const std::map<std::string, std::tuple<double, double, double>>& balances) {
    std::lock_guard<std::mutex> lock(mutex_);
    spot_positions_.clear();
    spot_reserved_.clear();
    for (const auto& [currency, balance] : balances) {
        SpotPosition position;
        position.currency = currency;
        position.available = std::get<0>(balance);
        position.frozen = std::get<1>(balance);
        position.borrowed = std::get<2>(balance);
        spot_positions_[currency] = position;
        INFO("Init spot position: [CURRENCY] " + currency + ", [AVAILABLE] " + std::to_string(position.available) +
             ", [TOTAL_FROZEN] " + std::to_string(position.frozen) + ", [BORROWED] " +
             std::to_string(position.borrowed));
    }
}

void PositionManager::SyncSpotFromExchange(const std::string& currency, double exchange_available,
                                           double exchange_frozen, double exchange_borrowed) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spot_positions_.find(currency);
    if (it == spot_positions_.end()) {
        if (exchange_available > 1e-8 || exchange_frozen > 1e-8 || exchange_borrowed > 1e-8) {
            ERROR("Spot first-sighting non-zero (likely external deposit, non-compliant): [CURRENCY] " + currency +
                  ", [AVAILABLE] " + std::to_string(exchange_available) + ", [FROZEN] " +
                  std::to_string(exchange_frozen) + ", [BORROWED] " + std::to_string(exchange_borrowed));
        }
        SpotPosition position;
        position.currency = currency;
        position.available = exchange_available;
        position.frozen = exchange_frozen;
        position.borrowed = exchange_borrowed;
        spot_positions_[currency] = position;
        return;
    }

    auto& position = it->second;
    double available_diff = std::abs(position.available - exchange_available);
    double frozen_diff = std::abs(position.frozen - exchange_frozen);
    double borrowed_diff = std::abs(position.borrowed - exchange_borrowed);

    if (available_diff > 1e-8 || frozen_diff > 1e-8 || borrowed_diff > 1e-8) {
        ERROR("Spot position mismatch: [CURRENCY] " + currency + ", [LOCAL_AVAILABLE] " +
              std::to_string(position.available) + ", [EXCHANGE_AVAILABLE] " + std::to_string(exchange_available) +
              ", [LOCAL_FROZEN] " + std::to_string(position.frozen) + ", [EXCHANGE_FROZEN] " +
              std::to_string(exchange_frozen) + ", [LOCAL_BORROWED] " + std::to_string(position.borrowed) +
              ", [EXCHANGE_BORROWED] " + std::to_string(exchange_borrowed));
        position.available = exchange_available;
        position.frozen = exchange_frozen;
        position.borrowed = exchange_borrowed;
    }
}

void PositionManager::ReserveSpot(const std::string& currency, double amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    spot_reserved_[currency] += amount;
}

double PositionManager::GetEffectiveAvailableSpot(const std::string& currency) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spot_positions_.find(currency);
    double available = (it != spot_positions_.end()) ? it->second.available : 0.0;
    auto res_it = spot_reserved_.find(currency);
    double reserved = (res_it != spot_reserved_.end()) ? res_it->second : 0.0;
    return available - reserved;
}

double PositionManager::GetBorrowed(const std::string& currency) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spot_positions_.find(currency);
    return (it != spot_positions_.end()) ? it->second.borrowed : 0.0;
}

bool PositionManager::CanBorrowMore(const std::string& currency, double amount, double max_borrow) const {
    if (max_borrow <= 0.0) {
        return true;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spot_positions_.find(currency);
    double current_borrowed = (it != spot_positions_.end()) ? it->second.borrowed : 0.0;
    return current_borrowed + amount <= max_borrow;
}

void PositionManager::UpdateSpotOnNew(const ExecutionReport& report, TradeMode trade_mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& info = InstrumentRegistry::Instance().Get(report.instrument);
    if (report.side == Side::BUY) {
        std::string quote_currency = ExtractCurrency<CurrencyPart::Quote>(report.instrument);
        auto& quote_position = spot_positions_[quote_currency];
        quote_position.currency = quote_currency;
        double freeze_amount =
            Decode(report.price, info.price_precision) * Decode(report.total_volume, info.volume_precision);
        spot_reserved_[quote_currency] = std::max(0.0, spot_reserved_[quote_currency] - freeze_amount);
        quote_position.available -= freeze_amount;
        quote_position.frozen += freeze_amount;
    } else if (trade_mode == TradeMode::CASH) {
        std::string base_currency = ExtractCurrency<CurrencyPart::Base>(report.instrument);
        auto& base_position = spot_positions_[base_currency];
        base_position.currency = base_currency;
        double freeze_amount = Decode(report.total_volume, info.volume_precision);
        spot_reserved_[base_currency] = std::max(0.0, spot_reserved_[base_currency] - freeze_amount);
        base_position.available -= freeze_amount;
        base_position.frozen += freeze_amount;
    }
}

void PositionManager::UpdateSpotOnFill(const ExecutionReport& report, TradeMode trade_mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string base_currency = ExtractCurrency<CurrencyPart::Base>(report.instrument);
    std::string quote_currency = ExtractCurrency<CurrencyPart::Quote>(report.instrument);

    if (report.status == OrderStatus::FILLED || report.status == OrderStatus::PARTIALLY_FILLED) {
        std::string order_id = std::string(report.order_id);
        auto& tracker = spot_order_fill_tracker_[order_id];
        Volume incremental_scaled = report.filled_volume - tracker;
        if (report.status == OrderStatus::FILLED) {
            spot_order_fill_tracker_.erase(order_id);
        } else {
            tracker = report.filled_volume;
        }
        if (incremental_scaled <= 0) {
            return;
        }

        const auto& info = InstrumentRegistry::Instance().Get(report.instrument);
        double incremental = Decode(incremental_scaled, info.volume_precision);

        auto& base_position = spot_positions_[base_currency];
        auto& quote_position = spot_positions_[quote_currency];
        base_position.currency = base_currency;
        quote_position.currency = quote_currency;

        std::string fee_currency = std::string(report.fee_currency);
        if (report.side == Side::BUY) {
            quote_position.frozen -= report.avg_fill_price * incremental;
            base_position.available += incremental;
            if (fee_currency == base_currency) {
                base_position.available += report.fee;
            } else if (fee_currency == quote_currency) {
                quote_position.available += report.fee;
            }
        } else {
            if (trade_mode == TradeMode::CASH) {
                base_position.frozen -= incremental;
            }
            quote_position.available += report.avg_fill_price * incremental;
            if (fee_currency == quote_currency) {
                quote_position.available += report.fee;
            } else if (fee_currency == base_currency) {
                base_position.available += report.fee;
            }
        }
    }
}

void PositionManager::UpdateSpotOnCancel(const ExecutionReport& report, TradeMode trade_mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (report.status == OrderStatus::CANCELLED) {
        Volume remaining_scaled = report.total_volume - report.filled_volume;
        if (remaining_scaled > 0) {
            const auto& info = InstrumentRegistry::Instance().Get(report.instrument);
            if (report.side == Side::BUY) {
                std::string quote_currency = ExtractCurrency<CurrencyPart::Quote>(report.instrument);
                auto& quote_position = spot_positions_[quote_currency];
                quote_position.currency = quote_currency;
                double release_amount =
                    Decode(remaining_scaled, info.volume_precision) * Decode(report.price, info.price_precision);
                quote_position.frozen -= release_amount;
                quote_position.available += release_amount;
            } else if (trade_mode == TradeMode::CASH) {
                std::string base_currency = ExtractCurrency<CurrencyPart::Base>(report.instrument);
                auto& base_position = spot_positions_[base_currency];
                base_position.currency = base_currency;
                double remaining = Decode(remaining_scaled, info.volume_precision);
                base_position.frozen -= remaining;
                base_position.available += remaining;
            }
        }
        spot_order_fill_tracker_.erase(std::string(report.order_id));
    }
}

void PositionManager::UpdateSpotOnRejected(const ExecutionReport& report, TradeMode trade_mode) {
    std::string order_id = std::string(report.order_id);
    std::string instrument = std::string(report.instrument);
    std::string side_str = ToString(report.side);

    const auto& info = InstrumentRegistry::Instance().Get(report.instrument);

    std::lock_guard<std::mutex> lock(mutex_);
    if (report.side == Side::SELL && trade_mode != TradeMode::CASH) {
        ERROR("Margin order rejected: [ORDER_ID] " + order_id + ", [INSTRUMENT] " + instrument + ", [SIDE] " +
              side_str);
        return;
    }
    if (report.side == Side::SELL) {
        std::string base_currency = ExtractCurrency<CurrencyPart::Base>(report.instrument);
        auto& base_position = spot_positions_[base_currency];
        double total_volume = Decode(report.total_volume, info.volume_precision);
        if (base_position.frozen >= total_volume - 1e-8) {
            base_position.frozen -= total_volume;
            base_position.available += total_volume;
            ERROR("Order rejected: [ORDER_ID] " + order_id + ", [INSTRUMENT] " + instrument + ", [SIDE] " + side_str +
                  ", [RELEASED_FROZEN] " + Format(report.total_volume, info.volume_precision) + ", [AVAILABLE] " +
                  std::to_string(base_position.available));
        } else {
            spot_reserved_[base_currency] = std::max(0.0, spot_reserved_[base_currency] - total_volume);
            ERROR("Order rejected: [ORDER_ID] " + order_id + ", [INSTRUMENT] " + instrument + ", [SIDE] " + side_str +
                  ", [RELEASED_RESERVED] " + Format(report.total_volume, info.volume_precision));
        }
    } else if (report.side == Side::BUY) {
        std::string quote_currency = ExtractCurrency<CurrencyPart::Quote>(report.instrument);
        auto& quote_position = spot_positions_[quote_currency];
        double release_amount =
            Decode(report.price, info.price_precision) * Decode(report.total_volume, info.volume_precision);
        if (quote_position.frozen >= release_amount - 1e-8) {
            quote_position.frozen -= release_amount;
            quote_position.available += release_amount;
            ERROR("Order rejected: [ORDER_ID] " + order_id + ", [INSTRUMENT] " + instrument + ", [SIDE] " + side_str +
                  ", [RELEASED_FROZEN] " + Format(report.price, info.price_precision) + "*" +
                  Format(report.total_volume, info.volume_precision) + ", [AVAILABLE] " +
                  std::to_string(quote_position.available));
        } else {
            spot_reserved_[quote_currency] = std::max(0.0, spot_reserved_[quote_currency] - release_amount);
            ERROR("Order rejected: [ORDER_ID] " + order_id + ", [INSTRUMENT] " + instrument + ", [SIDE] " + side_str +
                  ", [RELEASED_RESERVED] " + Format(report.price, info.price_precision) + "*" +
                  Format(report.total_volume, info.volume_precision));
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
    auto it = swap_positions_.find(key);
    if (it == swap_positions_.end()) {
        if (contracts > 1e-8) {
            ERROR("Swap first-sighting non-zero (likely external position, non-compliant): [INSTRUMENT] " + instrument +
                  ", [POSITION_SIDE] " + ToString(position_side) + ", [CONTRACTS] " + std::to_string(contracts) +
                  ", [AVERAGE_OPENING_PRICE] " + std::to_string(average_opening_price));
        }
        SwapPosition position;
        position.instrument = instrument;
        position.position_side = position_side;
        position.contracts = contracts;
        position.average_opening_price = average_opening_price;
        swap_positions_[key] = position;
        return;
    }

    auto& position = it->second;
    double contracts_diff = std::abs(position.contracts - contracts);
    if (contracts_diff > 1e-8) {
        ERROR("Swap position mismatch: [INSTRUMENT] " + instrument + ", [POSITION_SIDE] " + ToString(position_side) +
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

    std::string order_id = std::string(report.order_id);
    auto& tracker = swap_order_fill_tracker_[order_id];
    Volume incremental_scaled = report.filled_volume - tracker;
    if (report.status == OrderStatus::FILLED) {
        swap_order_fill_tracker_.erase(order_id);
    } else {
        tracker = report.filled_volume;
    }
    if (incremental_scaled <= 0) {
        return;
    }

    const auto& info = InstrumentRegistry::Instance().Get(report.instrument);
    double incremental = Decode(incremental_scaled, info.volume_precision);

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
        position.contracts += incremental;
    } else {
        position.contracts -= incremental;
        if (position.contracts < 0.0) {
            position.contracts = 0.0;
        }
    }

    INFO("Update swap position on fill: [INSTRUMENT] " + instrument + ", [POSITION_SIDE] " +
         ToString(report.position_side) + ", [SIDE] " + ToString(report.side) + ", [FILLED_VOLUME] " +
         Format(report.filled_volume, info.volume_precision) + ", [CONTRACTS] " + std::to_string(position.contracts));
}

void PositionManager::UpdateSwapOnCancel(const ExecutionReport& report) {
    swap_order_fill_tracker_.erase(std::string(report.order_id));
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
