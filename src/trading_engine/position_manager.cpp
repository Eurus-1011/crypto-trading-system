#include "position_manager.hpp"

void PositionManager::InitFromExchange(const std::map<std::string, std::pair<double, double>>& balances) {
    std::lock_guard<std::mutex> lock(mutex_);
    positions_.clear();
    for (auto& [currency, bal] : balances) {
        Position pos;
        pos.currency = currency;
        pos.available = bal.first;
        pos.frozen = bal.second;
        positions_[currency] = pos;
        INFO("Init position: [CURRENCY] " + currency + ", [AVAILABLE] " + std::to_string(pos.available) +
             ", [TOTAL_FROZEN] " + std::to_string(pos.frozen));
    }
}

void PositionManager::SyncFromExchange(const std::string& currency, double exchange_available, double exchange_frozen) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& pos = positions_[currency];
    pos.currency = currency;

    double avail_diff = std::abs(pos.available - exchange_available);
    double frozen_diff = std::abs(pos.frozen - exchange_frozen);

    if (avail_diff > 1e-8 || frozen_diff > 1e-8) {
        WARN("Position mismatch: [CURRENCY] " + currency + ", [LOCAL_AVAIL] " + std::to_string(pos.available) +
             ", [EXCHANGE_AVAIL] " + std::to_string(exchange_available) + ", [LOCAL_FROZEN] " +
             std::to_string(pos.frozen) + ", [EXCHANGE_FROZEN] " + std::to_string(exchange_frozen));
        pos.available = exchange_available;
        pos.frozen = exchange_frozen;
    }
}

void PositionManager::UpdateOnNew(const ExecutionReport& report) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (report.side == Side::BUY) {
        std::string quote_currency = ExtractQuoteCurrency(report.instrument);
        auto& quote_pos = positions_[quote_currency];
        quote_pos.currency = quote_currency;
        double freeze_amount = report.price * report.total_volume;
        quote_pos.available -= freeze_amount;
        quote_pos.frozen += freeze_amount;
        INFO("Freeze on new order: [CURRENCY] " + quote_currency + ", [FROZEN] " + std::to_string(freeze_amount) +
             ", [AVAILABLE] " + std::to_string(quote_pos.available) + ", [TOTAL_FROZEN] " +
             std::to_string(quote_pos.frozen));
    } else {
        std::string base_currency = ExtractBaseCurrency(report.instrument);
        auto& base_pos = positions_[base_currency];
        base_pos.currency = base_currency;
        double freeze_amount = report.total_volume;
        base_pos.available -= freeze_amount;
        base_pos.frozen += freeze_amount;
        INFO("Freeze on new order: [CURRENCY] " + base_currency + ", [FROZEN] " + std::to_string(freeze_amount) +
             ", [AVAILABLE] " + std::to_string(base_pos.available) + ", [TOTAL_FROZEN] " +
             std::to_string(base_pos.frozen));
    }
}

void PositionManager::UpdateOnFill(const ExecutionReport& report) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string base_currency = ExtractBaseCurrency(report.instrument);

    if (report.status == OrderStatus::FILLED || report.status == OrderStatus::PARTIALLY_FILLED) {
        auto& base_pos = positions_[base_currency];
        base_pos.currency = base_currency;

        if (report.side == Side::BUY) {
            base_pos.available += report.filled_volume;
        } else {
            base_pos.frozen -= report.filled_volume;
        }

        INFO("Update position on fill: [CURRENCY] " + base_currency + ", [SIDE] " + ToString(report.side) +
             ", [AVAILABLE] " + std::to_string(base_pos.available) + ", [TOTAL_FROZEN] " +
             std::to_string(base_pos.frozen));
    }
}

void PositionManager::UpdateOnCancel(const ExecutionReport& report) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (report.status == OrderStatus::CANCELLED) {
        double remaining = report.total_volume - report.filled_volume;
        if (remaining > 1e-8) {
            if (report.side == Side::BUY) {
                std::string quote_currency = ExtractQuoteCurrency(report.instrument);
                auto& quote_pos = positions_[quote_currency];
                quote_pos.currency = quote_currency;
                double release_amount = remaining * report.price;
                quote_pos.frozen -= release_amount;
                quote_pos.available += release_amount;
                INFO("Release frozen on cancel: [CURRENCY] " + quote_currency + ", [RELEASED] " +
                     std::to_string(release_amount) + ", [AVAILABLE] " + std::to_string(quote_pos.available) +
                     ", [TOTAL_FROZEN] " + std::to_string(quote_pos.frozen));
            } else {
                std::string base_currency = ExtractBaseCurrency(report.instrument);
                auto& base_pos = positions_[base_currency];
                base_pos.currency = base_currency;
                base_pos.frozen -= remaining;
                base_pos.available += remaining;
                INFO("Release frozen on cancel: [CURRENCY] " + base_currency + ", [RELEASED] " +
                     std::to_string(remaining) + ", [AVAILABLE] " + std::to_string(base_pos.available) +
                     ", [TOTAL_FROZEN] " + std::to_string(base_pos.frozen));
            }
        }
        INFO("Order cancelled: [ORDER_ID] " + std::string(report.order_id) + ", [INSTRUMENT] " +
             std::string(report.instrument));
    }
}

Position PositionManager::GetPosition(const std::string& currency) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto iter = positions_.find(currency);
    if (iter != positions_.end()) {
        return iter->second;
    }
    return Position{currency, 0.0, 0.0};
}

std::string PositionManager::ExtractBaseCurrency(const char* instrument) {
    std::string inst(instrument);
    auto dash_pos = inst.find('-');
    if (dash_pos != std::string::npos) {
        return inst.substr(0, dash_pos);
    }
    return inst;
}

std::string PositionManager::ExtractQuoteCurrency(const char* instrument) {
    std::string inst(instrument);
    auto dash_pos = inst.find('-');
    if (dash_pos != std::string::npos) {
        return inst.substr(dash_pos + 1);
    }
    return inst;
}
