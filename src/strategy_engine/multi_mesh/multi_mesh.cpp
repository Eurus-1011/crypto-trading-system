#include "multi_mesh.hpp"

#include "common/logger.hpp"
#include "common/utils.hpp"
#include "strategy_engine/strategy_registry.hpp"

#include <cmath>

static PosSide ParsePosSide(const std::string& s) {
    if (s == "long")
        return PosSide::LONG;
    else if (s == "short")
        return PosSide::SHORT;
    else if (s == "net")
        return PosSide::NET;
    else
        std::unreachable();
}

REGISTER_STRATEGY(MultiMeshStrategy)

void MultiMeshStrategy::Init(const Json::Value& params) {
    if (!params.isMember("mesh") || !params["mesh"].isArray()) {
        ERROR("MultiMesh init failed, [REASON] missing or invalid 'mesh' array");
        Stop();
        return;
    }

    fee_rate_ = params.get("fee_rate", 0.001).asDouble();

    const auto& mesh_configs = params["mesh"];
    for (Json::ArrayIndex i = 0; i < mesh_configs.size(); ++i) {
        InitMesh(mesh_configs[i], fee_rate_);
    }

    if (meshes_.empty()) {
        ERROR("MultiMesh init failed, [REASON] no valid mesh configured");
        Stop();
        return;
    }

    INFO("MultiMesh init success, [MESH_COUNT] " + std::to_string(meshes_.size()));
}

void MultiMeshStrategy::InitMesh(const Json::Value& config, double fee_rate) {
    std::string instrument = config.get("instrument", "").asString();
    if (instrument.empty()) {
        return;
    }

    auto dash_pos = instrument.find('-');
    if (dash_pos == std::string::npos) {
        ERROR("MultiMesh init failed, [REASON] invalid instrument format, [INSTRUMENT] " + instrument);
        return;
    }

    MeshConfig mesh;
    mesh.instrument = instrument;
    mesh.base_currency = instrument.substr(0, dash_pos);
    mesh.quote_currency = instrument.substr(dash_pos + 1);
    mesh.market_type = DetectMarketType(instrument.c_str());

    std::string position_side_str = config.get("position_side", "").asString();
    if (position_side_str.empty()) {
        ERROR("MultiMesh init failed, [REASON] missing 'position_side', [INSTRUMENT] " + instrument);
        return;
    }
    mesh.position_side = ParsePosSide(position_side_str);
    if (mesh.market_type == MarketType::SWAP && mesh.position_side == PosSide::NET) {
        WARN("MultiMesh: SWAP instrument with position_side=net, [INSTRUMENT] " + instrument);
    }

    mesh.upper_price = config.get("upper_price", 0.0).asDouble();
    mesh.lower_price = config.get("lower_price", 0.0).asDouble();
    mesh.grid_count = config.get("grid_count", 10).asInt();
    mesh.grid_volume = config.get("grid_volume", 0.0).asDouble();

    if (mesh.upper_price <= mesh.lower_price || mesh.grid_count <= 0 || mesh.grid_volume <= 0) {
        ERROR("MultiMesh init failed, [REASON] invalid params, [INSTRUMENT] " + instrument + ", [UPPER] " +
              std::to_string(mesh.upper_price) + ", [LOWER] " + std::to_string(mesh.lower_price) + ", [GRID_COUNT] " +
              std::to_string(mesh.grid_count));
        return;
    }

    mesh.grid_step = (mesh.upper_price - mesh.lower_price) / mesh.grid_count;
    double mid_price = (mesh.upper_price + mesh.lower_price) / 2.0;
    double grid_profit_rate = mesh.grid_step / mid_price;
    double round_trip_fee_rate = fee_rate * 2.0;

    if (grid_profit_rate <= round_trip_fee_rate) {
        ERROR("MultiMesh init failed, [REASON] grid profit rate below fee, [INSTRUMENT] " + instrument +
              ", [GRID_PROFIT] " + std::to_string(grid_profit_rate * 100) + "%, [ROUND_TRIP_FEE] " +
              std::to_string(round_trip_fee_rate * 100) + "%");
        return;
    }

    mesh.grids.resize(mesh.grid_count + 1);
    for (int idx = 0; idx <= mesh.grid_count; ++idx) {
        mesh.grids[idx].price = mesh.lower_price + idx * mesh.grid_step;
        mesh.grids[idx].volume = mesh.grid_volume;
    }

    INFO("MultiMesh instrument init success, [INSTRUMENT] " + instrument + ", [RANGE] " +
         std::to_string(mesh.lower_price) + " ~ " + std::to_string(mesh.upper_price) + ", [GRIDS] " +
         std::to_string(mesh.grid_count) + ", [STEP] " + std::to_string(mesh.grid_step) + ", [GRID_VOLUME] " +
         std::to_string(mesh.grid_volume) + ", [NET_PROFIT_RATE] " +
         std::to_string((grid_profit_rate - round_trip_fee_rate) * 100) + "%");

    meshes_[instrument] = std::move(mesh);
}

void MultiMeshStrategy::Reconstruct(const std::vector<ExecutionReport>& pending_orders) {
    int adopted = 0;
    int cancelled = 0;

    for (const auto& report : pending_orders) {
        MeshConfig* mesh = FindMesh(report.instrument);
        if (!mesh) {
            continue;
        }

        if (TryAdoptOrder(mesh, report)) {
            ++adopted;
        } else {
            EmitCancel(report.instrument, report.order_id, report.market_type);
            ++cancelled;
            INFO("Cancel unmatched pending order, [ORDER_ID] " + std::string(report.order_id) + ", [PRICE] " +
                 std::to_string(report.price) + ", [SIDE] " + ToString(report.side));
        }
    }

    INFO("Reconstruct from pending orders, [ADOPTED] " + std::to_string(adopted) + ", [CANCELLED] " +
         std::to_string(cancelled));
}

bool MultiMeshStrategy::TryAdoptOrder(MeshConfig* mesh, const ExecutionReport& report) {
    for (int idx = 0; idx <= mesh->grid_count; ++idx) {
        if (std::abs(mesh->grids[idx].price - report.price) < mesh->grid_step * 0.1) {
            if (report.side == Side::BUY && mesh->grids[idx].state == GridState::EMPTY) {
                mesh->grids[idx].state = GridState::BUY_PENDING;
                mesh->grids[idx].order_id = report.order_id;
                INFO("Adopt existing BUY order, [INSTRUMENT] " + mesh->instrument + ", [GRID] " + std::to_string(idx) +
                     ", [ORDER_ID] " + std::string(report.order_id) + ", [PRICE] " + std::to_string(report.price));
                return true;
            }
            if (report.side == Side::SELL) {
                if (mesh->grids[idx].state == GridState::EMPTY || mesh->grids[idx].state == GridState::BOUGHT) {
                    mesh->grids[idx].state = GridState::SELL_PENDING;
                    mesh->grids[idx].order_id = report.order_id;
                    if (idx > 0 && mesh->grids[idx - 1].state == GridState::EMPTY) {
                        mesh->grids[idx - 1].state = GridState::BOUGHT;
                        mesh->grids[idx - 1].buy_fill_price = mesh->grids[idx - 1].price;
                    }
                    INFO("Adopt existing SELL order, [INSTRUMENT] " + mesh->instrument + ", [GRID] " +
                         std::to_string(idx) + ", [ORDER_ID] " + std::string(report.order_id) + ", [PRICE] " +
                         std::to_string(report.price));
                    return true;
                }
            }
            break;
        }
    }
    return false;
}

void MultiMeshStrategy::OnBBO(const BBO& bbo) {
    MeshConfig* mesh = FindMesh(bbo.instrument);
    if (!mesh) {
        return;
    }

    mesh->last_bid = bbo.bid_price;
    mesh->last_ask = bbo.ask_price;
    mesh->last_bbo_ts_ns = bbo.local_ts_ns;

    if (!mesh->initialized) {
        double mid_price = (bbo.bid_price + bbo.ask_price) / 2.0;
        int buy_count = 0;
        int sell_count = 0;

        if (mesh->market_type == MarketType::SPOT) {
            double quote_remaining = position_manager_->GetSpotPosition(mesh->quote_currency).available;
            double quote_before = quote_remaining;
            double base_before = position_manager_->GetSpotPosition(mesh->base_currency).available;

            for (int idx = mesh->grid_count; idx >= 0; --idx) {
                if (mesh->grids[idx].state != GridState::EMPTY)
                    continue;
                if (mesh->grids[idx].price >= mid_price - mesh->grid_step * 0.5)
                    continue;
                double cost = mesh->grids[idx].price * mesh->grids[idx].volume;
                if (quote_remaining < cost)
                    break;
                PlaceBuyAtGrid(mesh, idx);
                quote_remaining -= cost;
                ++buy_count;
            }
            for (int idx = 0; idx <= mesh->grid_count; ++idx) {
                if (mesh->grids[idx].state != GridState::EMPTY)
                    continue;
                if (mesh->grids[idx].price < mid_price + mesh->grid_step * 0.5)
                    continue;
                PlaceSellAtGrid(mesh, idx);
                ++sell_count;
            }
            INFO("Place initial grid orders, [INSTRUMENT] " + mesh->instrument + ", [MID_PRICE] " +
                 std::to_string(mid_price) + ", [BUY_GRIDS] " + std::to_string(buy_count) + ", [SELL_GRIDS] " +
                 std::to_string(sell_count) + ", [QUOTE_USED] " + std::to_string(quote_before - quote_remaining) +
                 ", [BASE_USED] " +
                 std::to_string(base_before - position_manager_->GetSpotPosition(mesh->base_currency).available));
        } else {
            for (int idx = mesh->grid_count; idx >= 0; --idx) {
                if (mesh->grids[idx].state != GridState::EMPTY)
                    continue;
                if (mesh->grids[idx].price >= mid_price - mesh->grid_step * 0.5)
                    continue;
                PlaceBuyAtGrid(mesh, idx);
                ++buy_count;
            }
            for (int idx = 0; idx <= mesh->grid_count; ++idx) {
                if (mesh->grids[idx].state != GridState::EMPTY)
                    continue;
                if (mesh->grids[idx].price < mid_price + mesh->grid_step * 0.5)
                    continue;
                PlaceSellAtGrid(mesh, idx);
                ++sell_count;
            }
            INFO("Place initial grid orders, [INSTRUMENT] " + mesh->instrument + ", [MID_PRICE] " +
                 std::to_string(mid_price) + ", [BUY_GRIDS] " + std::to_string(buy_count) + ", [SELL_GRIDS] " +
                 std::to_string(sell_count));
        }
        mesh->initialized = true;
    }
}

void MultiMeshStrategy::OnExecutionReport(const ExecutionReport& report) {
    MeshConfig* mesh = FindMesh(report.instrument);
    if (!mesh) {
        return;
    }

    int grid_index = FindGridByOrderId(mesh, std::string(report.order_id));

    if (grid_index < 0 && report.status == OrderStatus::NEW) {
        GridState expected = (report.side == Side::BUY) ? GridState::BUY_PENDING : GridState::SELL_PENDING;
        grid_index = FindGridByPriceAndState(mesh, report.price, expected);
        if (grid_index >= 0) {
            mesh->grids[grid_index].order_id = report.order_id;
            INFO("Assign order id to grid, [INSTRUMENT] " + mesh->instrument + ", [GRID] " +
                 std::to_string(grid_index) + ", [ORDER_ID] " + std::string(report.order_id));
        }
        return;
    }

    if (grid_index < 0 && report.status == OrderStatus::REJECTED) {
        GridState expected = (report.side == Side::BUY) ? GridState::BUY_PENDING : GridState::SELL_PENDING;
        grid_index = FindGridByPriceAndState(mesh, report.price, expected);
    }

    if (grid_index < 0) {
        return;
    }

    auto& grid = mesh->grids[grid_index];

    if (report.status == OrderStatus::FILLED) {
        double fill_value = report.avg_fill_price * report.filled_volume;
        double fee = fill_value * fee_rate_;
        mesh->total_fee += fee;

        if (grid.state == GridState::BUY_PENDING) {
            grid.state = GridState::BOUGHT;
            grid.buy_fill_price = report.avg_fill_price;
            grid.order_id.clear();
            INFO("Grid order filled, [INSTRUMENT] " + mesh->instrument + ", [SIDE] BUY, [GRID] " +
                 std::to_string(grid_index) + ", [PRICE] " + std::to_string(report.avg_fill_price) + ", [VOLUME] " +
                 std::to_string(report.filled_volume) + ", [FEE] " + std::to_string(fee));

            if (grid_index < mesh->grid_count) {
                PlaceSellAtGrid(mesh, grid_index + 1);
            }
        } else if (grid.state == GridState::SELL_PENDING) {
            int buy_grid = grid_index - 1;
            double buy_price = (buy_grid >= 0 && mesh->grids[buy_grid].buy_fill_price > 0)
                                   ? mesh->grids[buy_grid].buy_fill_price
                                   : mesh->grids[buy_grid >= 0 ? buy_grid : grid_index].price;
            double gross_profit = (report.avg_fill_price - buy_price) * report.filled_volume;
            double buy_fee = buy_price * report.filled_volume * fee_rate_;
            double net_profit = gross_profit - fee - buy_fee;
            mesh->total_profit += net_profit;
            ++mesh->total_round_trips;

            grid.state = GridState::EMPTY;
            grid.order_id.clear();
            INFO("Grid order filled, [INSTRUMENT] " + mesh->instrument + ", [SIDE] SELL, [GRID] " +
                 std::to_string(grid_index) + ", [BUY_PRICE] " + std::to_string(buy_price) + ", [SELL_PRICE] " +
                 std::to_string(report.avg_fill_price) + ", [VOLUME] " + std::to_string(report.filled_volume) +
                 ", [NET_PROFIT] " + std::to_string(net_profit) + ", [TOTAL_PROFIT] " +
                 std::to_string(mesh->total_profit) + ", [TOTAL_FEE] " + std::to_string(mesh->total_fee) +
                 ", [ROUND_TRIPS] " + std::to_string(mesh->total_round_trips));

            if (buy_grid >= 0) {
                PlaceBuyAtGrid(mesh, buy_grid);
            }
        }
    } else if (report.status == OrderStatus::CANCELLED) {
        INFO("Grid order cancelled, [INSTRUMENT] " + mesh->instrument + ", [GRID] " + std::to_string(grid_index) +
             ", [ORDER_ID] " + std::string(report.order_id));
        if (grid.state == GridState::BUY_PENDING) {
            grid.state = GridState::EMPTY;
        } else if (grid.state == GridState::SELL_PENDING) {
            grid.state = GridState::BOUGHT;
        }
        grid.order_id.clear();
    } else if (report.status == OrderStatus::CANCEL_FAILED) {
        INFO("Grid cancel failed, likely already filled, [INSTRUMENT] " + mesh->instrument + ", [GRID] " +
             std::to_string(grid_index) + ", [ORDER_ID] " + std::string(report.order_id));
    } else if (report.status == OrderStatus::REJECTED) {
        ERROR("Grid order rejected, [INSTRUMENT] " + mesh->instrument + ", [GRID] " + std::to_string(grid_index) +
              ", [ORDER_ID] " + std::string(report.order_id));
        if (grid.state == GridState::BUY_PENDING) {
            grid.state = GridState::EMPTY;
            PlaceBuyAtGrid(mesh, grid_index);
        } else if (grid.state == GridState::SELL_PENDING) {
            grid.state = GridState::BOUGHT;
            PlaceSellAtGrid(mesh, grid_index);
        }
        grid.order_id.clear();
    }
}

void MultiMeshStrategy::OnTimer() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_heartbeat_ts_ < std::chrono::minutes(15)) {
        return;
    }
    last_heartbeat_ts_ = now;

    for (auto& [instrument, mesh] : meshes_) {
        int buy_pending = 0, bought = 0, sell_pending = 0, empty = 0;
        for (int idx = 0; idx <= mesh.grid_count; ++idx) {
            switch (mesh.grids[idx].state) {
            case GridState::BUY_PENDING:
                ++buy_pending;
                break;
            case GridState::BOUGHT:
                ++bought;
                break;
            case GridState::SELL_PENDING:
                ++sell_pending;
                break;
            case GridState::EMPTY:
                ++empty;
                break;
            }
        }

        INFO("Heartbeat, [INSTRUMENT] " + instrument + ", [BID] " + std::to_string(mesh.last_bid) + ", [ASK] " +
             std::to_string(mesh.last_ask) + ", [BUY_PENDING] " + std::to_string(buy_pending) + ", [BOUGHT] " +
             std::to_string(bought) + ", [SELL_PENDING] " + std::to_string(sell_pending) + ", [EMPTY] " +
             std::to_string(empty) + ", [TOTAL_PROFIT] " + std::to_string(mesh.total_profit) + ", [ROUND_TRIPS] " +
             std::to_string(mesh.total_round_trips));
    }
}

MeshConfig* MultiMeshStrategy::FindMesh(const char* instrument) {
    auto it = meshes_.find(instrument);
    return it != meshes_.end() ? &it->second : nullptr;
}

const MeshConfig* MultiMeshStrategy::FindMesh(const char* instrument) const {
    auto it = meshes_.find(instrument);
    return it != meshes_.end() ? &it->second : nullptr;
}

int MultiMeshStrategy::FindGridByOrderId(MeshConfig* mesh, const std::string& order_id) const {
    if (order_id.empty()) {
        return -1;
    }
    for (int idx = 0; idx <= mesh->grid_count; ++idx) {
        if (!mesh->grids[idx].order_id.empty() && mesh->grids[idx].order_id == order_id) {
            return idx;
        }
    }
    return -1;
}

int MultiMeshStrategy::FindGridByPriceAndState(MeshConfig* mesh, double price, GridState expected_state) const {
    for (int idx = 0; idx <= mesh->grid_count; ++idx) {
        if (mesh->grids[idx].state == expected_state &&
            std::abs(mesh->grids[idx].price - price) < mesh->grid_step * 0.1) {
            return idx;
        }
    }
    return -1;
}

void MultiMeshStrategy::PlaceBuyAtGrid(MeshConfig* mesh, int grid_index) {
    if (grid_index < 0 || grid_index > mesh->grid_count) {
        return;
    }
    auto& grid = mesh->grids[grid_index];
    if (grid.state != GridState::EMPTY) {
        return;
    }

    grid.state = GridState::BUY_PENDING;
    grid.order_sent_ts_ns = NowNs();
    grid.order_id.clear();
    EmitBuy(mesh->instrument.c_str(), OrderType::LIMIT, grid.price, grid.volume, mesh->market_type,
            mesh->position_side);

    INFO("Place grid order, [INSTRUMENT] " + mesh->instrument + ", [SIDE] BUY, [GRID] " + std::to_string(grid_index) +
         ", [PRICE] " + std::to_string(grid.price) + ", [VOLUME] " + std::to_string(grid.volume));
}

void MultiMeshStrategy::PlaceSellAtGrid(MeshConfig* mesh, int grid_index) {
    if (grid_index < 0 || grid_index > mesh->grid_count) {
        return;
    }
    auto& grid = mesh->grids[grid_index];
    if (grid.state != GridState::EMPTY && grid.state != GridState::BOUGHT) {
        return;
    }
    if (mesh->market_type == MarketType::SPOT) {
        if (position_manager_->GetSpotPosition(mesh->base_currency).available < grid.volume) {
            return;
        }
    } else {
        if (position_manager_->GetSwapPosition(mesh->instrument, mesh->position_side).contracts < grid.volume) {
            return;
        }
    }

    grid.state = GridState::SELL_PENDING;
    grid.order_sent_ts_ns = NowNs();
    grid.order_id.clear();
    if (mesh->market_type == MarketType::SPOT) {
        position_manager_->DeductSpot(mesh->base_currency, grid.volume);
    }
    EmitSell(mesh->instrument.c_str(), OrderType::LIMIT, grid.price, grid.volume, mesh->market_type,
             mesh->position_side);

    INFO("Place grid order, [INSTRUMENT] " + mesh->instrument + ", [SIDE] SELL, [GRID] " + std::to_string(grid_index) +
         ", [PRICE] " + std::to_string(grid.price) + ", [VOLUME] " + std::to_string(grid.volume));
}
