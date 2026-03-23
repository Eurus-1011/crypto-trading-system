#include "position_manager.hpp"

void PositionManager::UpdateOnFill(const ExecutionReport& report) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string base_currency = ExtractBaseCurrency(report.instrument);
    auto& position = positions_[base_currency];
    position.currency = base_currency;

    if (report.status == OrderStatus::FILLED || report.status == OrderStatus::PARTIALLY_FILLED) {
        if (report.side == Side::BUY) {
            position.available += report.filled_volume;
        } else {
            position.available -= report.filled_volume;
        }
        INFO("Update position on fill: [CURRENCY] " + base_currency + ", [AVAILABLE] " +
             std::to_string(position.available) + ", [FILLED] " + std::to_string(report.filled_volume) + ", [SIDE] " +
             std::string(report.side == Side::BUY ? "buy" : "sell"));
    }
}

void PositionManager::UpdateOnCancel(const ExecutionReport& report) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (report.status == OrderStatus::CANCELLED) {
        INFO("Order cancelled: [ORDER_ID] " + std::string(report.order_id) + ", [INSTRUMENT] " +
             std::string(report.instrument));
    }
}

void PositionManager::SyncFromExchange(const std::string& currency, double exchange_available) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& position = positions_[currency];
    position.currency = currency;
    position.last_sync_ts_ns = NowNs();

    double diff = std::abs(position.available - exchange_available);
    if (diff > 1e-8) {
        WARN("Position mismatch detected: [CURRENCY] " + currency + ", [LOCAL] " + std::to_string(position.available) +
             ", [EXCHANGE] " + std::to_string(exchange_available) + ", [DIFF] " + std::to_string(diff));
        position.available = exchange_available;
    }
}

Position PositionManager::GetPosition(const std::string& currency) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto iter = positions_.find(currency);
    if (iter != positions_.end()) {
        return iter->second;
    }
    return Position{currency, 0.0, 0.0, 0};
}

std::string PositionManager::ExtractBaseCurrency(const char* instrument) {
    std::string inst(instrument);
    auto dash_pos = inst.find('-');
    if (dash_pos != std::string::npos) {
        return inst.substr(0, dash_pos);
    }
    return inst;
}
