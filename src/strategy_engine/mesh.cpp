#include "mesh.hpp"

#include "common/logger.hpp"

#include <cmath>

void MeshStrategy::SetBalances(const std::map<std::string, std::pair<double, double>>& balances) {
    auto dash_pos = instrument_.find('-');
    if (dash_pos == std::string::npos)
        return;

    std::string base = instrument_.substr(0, dash_pos);
    std::string quote = instrument_.substr(dash_pos + 1);

    if (auto it = balances.find(base); it != balances.end()) {
        base_available_ = it->second.first;
    }
    if (auto it = balances.find(quote); it != balances.end()) {
        quote_available_ = it->second.first;
    }
}

void MeshStrategy::Init(const Json::Value& params) {
    instrument_ = params.get("instrument", "BTC-USDT").asString();
    upper_price_ = params.get("upper_price", 0.0).asDouble();
    lower_price_ = params.get("lower_price", 0.0).asDouble();
    grid_count_ = params.get("grid_count", 10).asInt();
    grid_volume_ = params.get("grid_volume", 0.0).asDouble();
    fee_rate_ = params.get("fee_rate", 0.001).asDouble();

    if (upper_price_ <= lower_price_ || grid_count_ <= 0 || grid_volume_ <= 0) {
        ERROR("Mesh init failed: invalid params"
              ", [UPPER] " +
              std::to_string(upper_price_) + ", [LOWER] " + std::to_string(lower_price_) + ", [GRID_COUNT] " +
              std::to_string(grid_count_));
        Stop();
        return;
    }

    grid_step_ = (upper_price_ - lower_price_) / grid_count_;
    double mid_price = (upper_price_ + lower_price_) / 2.0;
    double grid_profit_rate = grid_step_ / mid_price;
    double round_trip_fee_rate = fee_rate_ * 2.0;

    if (grid_profit_rate <= round_trip_fee_rate) {
        ERROR("Mesh init failed: grid profit rate below fee"
              ", [GRID_PROFIT] " +
              std::to_string(grid_profit_rate * 100) + "%" + ", [ROUND_TRIP_FEE] " +
              std::to_string(round_trip_fee_rate * 100) + "%");
        Stop();
        return;
    }

    grids_.resize(grid_count_ + 1);
    for (int idx = 0; idx <= grid_count_; ++idx) {
        grids_[idx].price = lower_price_ + idx * grid_step_;
        grids_[idx].volume = grid_volume_;
    }

    INFO("Mesh init success: [INSTRUMENT] " + instrument_ + ", [RANGE] " + std::to_string(lower_price_) + " ~ " +
         std::to_string(upper_price_) + ", [GRIDS] " + std::to_string(grid_count_) + ", [STEP] " +
         std::to_string(grid_step_) + ", [GRID_VOLUME] " + std::to_string(grid_volume_) + ", [NET_PROFIT_RATE] " +
         std::to_string((grid_profit_rate - round_trip_fee_rate) * 100) + "%");
}

void MeshStrategy::Reconstruct(const std::vector<ExecutionReport>& pending_orders) {
    int adopted = 0;
    int cancelled = 0;

    for (const auto& report : pending_orders) {
        if (std::string(report.instrument) != instrument_) {
            continue;
        }

        if (TryAdoptOrder(report)) {
            ++adopted;
        } else {
            EmitCancel(report.instrument, report.order_id);
            ++cancelled;
            INFO("Cancel unmatched pending order: [ORDER_ID] " + std::string(report.order_id) + ", [PRICE] " +
                 std::to_string(report.price) + ", [SIDE] " + ToString(report.side));
        }
    }

    INFO("Reconstruct from pending orders: [ADOPTED] " + std::to_string(adopted) + ", [CANCELLED] " +
         std::to_string(cancelled));
}

bool MeshStrategy::TryAdoptOrder(const ExecutionReport& report) {
    for (int idx = 0; idx <= grid_count_; ++idx) {
        if (std::abs(grids_[idx].price - report.price) < grid_step_ * 0.1) {
            if (report.side == Side::BUY && grids_[idx].state == GridState::EMPTY) {
                grids_[idx].state = GridState::BUY_PENDING;
                grids_[idx].order_id = report.order_id;
                INFO("Adopt existing BUY order: [GRID] " + std::to_string(idx) + ", [ORDER_ID] " +
                     std::string(report.order_id) + ", [PRICE] " + std::to_string(report.price));
                return true;
            }
            if (report.side == Side::SELL) {
                if (grids_[idx].state == GridState::EMPTY || grids_[idx].state == GridState::BOUGHT) {
                    grids_[idx].state = GridState::SELL_PENDING;
                    grids_[idx].order_id = report.order_id;
                    if (idx > 0 && grids_[idx - 1].state == GridState::EMPTY) {
                        grids_[idx - 1].state = GridState::BOUGHT;
                        grids_[idx - 1].buy_fill_price = grids_[idx - 1].price;
                    }
                    INFO("Adopt existing SELL order: [GRID] " + std::to_string(idx) + ", [ORDER_ID] " +
                         std::string(report.order_id) + ", [PRICE] " + std::to_string(report.price));
                    return true;
                }
            }
            break;
        }
    }
    return false;
}

void MeshStrategy::OnBBO(const BBO& bbo) {
    if (std::string(bbo.instrument) != instrument_) {
        return;
    }

    if (!initialized_) {
        double mid_price = (bbo.bid_price + bbo.ask_price) / 2.0;
        double quote_remaining = quote_available_;
        double base_remaining = base_available_;
        int buy_count = 0;
        int sell_count = 0;

        for (int idx = grid_count_; idx >= 0; --idx) {
            if (grids_[idx].state != GridState::EMPTY)
                continue;
            if (grids_[idx].price >= mid_price - grid_step_ * 0.5)
                continue;
            double cost = grids_[idx].price * grids_[idx].volume;
            if (quote_remaining < cost)
                break;
            PlaceBuyAtGrid(idx);
            quote_remaining -= cost;
            ++buy_count;
        }

        for (int idx = 0; idx <= grid_count_; ++idx) {
            if (grids_[idx].state != GridState::EMPTY)
                continue;
            if (grids_[idx].price < mid_price + grid_step_ * 0.5)
                continue;
            if (base_remaining < grids_[idx].volume)
                break;
            PlaceSellAtGrid(idx);
            base_remaining -= grids_[idx].volume;
            ++sell_count;
        }

        INFO("Place initial grid orders: [MID_PRICE] " + std::to_string(mid_price) + ", [BUY_GRIDS] " +
             std::to_string(buy_count) + ", [SELL_GRIDS] " + std::to_string(sell_count) + ", [QUOTE_USED] " +
             std::to_string(quote_available_ - quote_remaining) + ", [BASE_USED] " +
             std::to_string(base_available_ - base_remaining));
        initialized_ = true;
    }
}

void MeshStrategy::OnExecutionReport(const ExecutionReport& report) {
    if (std::string(report.instrument) != instrument_) {
        return;
    }

    int grid_index = FindGridByOrderId(std::string(report.order_id));

    if (grid_index < 0 && report.status == OrderStatus::NEW) {
        GridState expected = (report.side == Side::BUY) ? GridState::BUY_PENDING : GridState::SELL_PENDING;
        grid_index = FindGridByPriceAndState(report.price, expected);
        if (grid_index >= 0) {
            grids_[grid_index].order_id = report.order_id;
            INFO("Assign order id to grid: [GRID] " + std::to_string(grid_index) + ", [ORDER_ID] " +
                 std::string(report.order_id));
        }
        return;
    }

    if (grid_index < 0) {
        return;
    }

    auto& grid = grids_[grid_index];

    if (report.status == OrderStatus::FILLED) {
        double fill_value = report.avg_fill_price * report.filled_volume;
        double fee = fill_value * fee_rate_;
        total_fee_ += fee;

        if (grid.state == GridState::BUY_PENDING) {
            grid.state = GridState::BOUGHT;
            grid.buy_fill_price = report.avg_fill_price;
            grid.order_id.clear();
            INFO("Grid order filled: [INSTRUMENT] " + instrument_ + ", [SIDE] BUY, [GRID] " +
                 std::to_string(grid_index) + ", [PRICE] " + std::to_string(report.avg_fill_price) + ", [VOLUME] " +
                 std::to_string(report.filled_volume) + ", [FEE] " + std::to_string(fee));

            if (grid_index < grid_count_) {
                PlaceSellAtGrid(grid_index + 1);
            }
        } else if (grid.state == GridState::SELL_PENDING) {
            int buy_grid = grid_index - 1;
            double buy_price = (buy_grid >= 0 && grids_[buy_grid].buy_fill_price > 0)
                                   ? grids_[buy_grid].buy_fill_price
                                   : grids_[buy_grid >= 0 ? buy_grid : grid_index].price;
            double gross_profit = (report.avg_fill_price - buy_price) * report.filled_volume;
            double buy_fee = buy_price * report.filled_volume * fee_rate_;
            double net_profit = gross_profit - fee - buy_fee;
            total_profit_ += net_profit;
            ++total_round_trips_;

            grid.state = GridState::EMPTY;
            grid.order_id.clear();
            INFO("Grid order filled: [INSTRUMENT] " + instrument_ + ", [SIDE] SELL, [GRID] " +
                 std::to_string(grid_index) + ", [BUY_PRICE] " + std::to_string(buy_price) + ", [SELL_PRICE] " +
                 std::to_string(report.avg_fill_price) + ", [VOLUME] " + std::to_string(report.filled_volume) +
                 ", [NET_PROFIT] " + std::to_string(net_profit) + ", [TOTAL_PROFIT] " + std::to_string(total_profit_) +
                 ", [TOTAL_FEE] " + std::to_string(total_fee_) + ", [ROUND_TRIPS] " +
                 std::to_string(total_round_trips_));

            if (buy_grid >= 0) {
                PlaceBuyAtGrid(buy_grid);
            }
        }
    } else if (report.status == OrderStatus::CANCELLED) {
        INFO("Grid order cancelled: [GRID] " + std::to_string(grid_index) + ", [ORDER_ID] " +
             std::string(report.order_id));
        if (grid.state == GridState::BUY_PENDING) {
            grid.state = GridState::EMPTY;
        } else if (grid.state == GridState::SELL_PENDING) {
            grid.state = GridState::BOUGHT;
        }
        grid.order_id.clear();
    } else if (report.status == OrderStatus::CANCEL_FAILED) {
        INFO("Grid cancel failed, likely already filled: [GRID] " + std::to_string(grid_index) + ", [ORDER_ID] " +
             std::string(report.order_id));
    } else if (report.status == OrderStatus::REJECTED) {
        ERROR("Grid order rejected: [GRID] " + std::to_string(grid_index) + ", [ORDER_ID] " +
              std::string(report.order_id));
        if (grid.state == GridState::BUY_PENDING) {
            grid.state = GridState::EMPTY;
        } else if (grid.state == GridState::SELL_PENDING) {
            grid.state = GridState::BOUGHT;
        }
        grid.order_id.clear();
    }
}

void MeshStrategy::OnTimer() {}

int MeshStrategy::FindGridByOrderId(const std::string& order_id) const {
    if (order_id.empty()) {
        return -1;
    }
    for (int idx = 0; idx <= grid_count_; ++idx) {
        if (!grids_[idx].order_id.empty() && grids_[idx].order_id == order_id) {
            return idx;
        }
    }
    return -1;
}

int MeshStrategy::FindGridByPriceAndState(double price, GridState expected_state) const {
    for (int idx = 0; idx <= grid_count_; ++idx) {
        if (grids_[idx].state == expected_state && std::abs(grids_[idx].price - price) < grid_step_ * 0.1) {
            return idx;
        }
    }
    return -1;
}

void MeshStrategy::PlaceBuyAtGrid(int grid_index) {
    if (grid_index < 0 || grid_index > grid_count_) {
        return;
    }
    auto& grid = grids_[grid_index];
    if (grid.state != GridState::EMPTY) {
        return;
    }

    grid.state = GridState::BUY_PENDING;
    grid.order_sent_ts_ns = NowNs();
    grid.order_id.clear();
    EmitBuy(instrument_.c_str(), OrderType::LIMIT, grid.price, grid.volume);

    INFO("Place grid order: [INSTRUMENT] " + instrument_ + ", [SIDE] BUY, [GRID] " + std::to_string(grid_index) +
         ", [PRICE] " + std::to_string(grid.price) + ", [VOLUME] " + std::to_string(grid.volume));
}

void MeshStrategy::PlaceSellAtGrid(int grid_index) {
    if (grid_index < 0 || grid_index > grid_count_) {
        return;
    }
    auto& grid = grids_[grid_index];
    if (grid.state != GridState::EMPTY && grid.state != GridState::BOUGHT) {
        return;
    }

    grid.state = GridState::SELL_PENDING;
    grid.order_sent_ts_ns = NowNs();
    grid.order_id.clear();
    EmitSell(instrument_.c_str(), OrderType::LIMIT, grid.price, grid.volume);

    INFO("Place grid order: [INSTRUMENT] " + instrument_ + ", [SIDE] SELL, [GRID] " + std::to_string(grid_index) +
         ", [PRICE] " + std::to_string(grid.price) + ", [VOLUME] " + std::to_string(grid.volume));
}
