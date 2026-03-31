#include "multi_mesh.hpp"

#include "common/logger.hpp"
#include "strategy_engine/strategy_registry.hpp"

#include <cmath>

static bool ParsePosSide(const std::string& s, PosSide& out) {
    if (s == "long") {
        out = PosSide::LONG;
        return true;
    }
    if (s == "short") {
        out = PosSide::SHORT;
        return true;
    }
    if (s == "net") {
        out = PosSide::NET;
        return true;
    }
    return false;
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
    mesh.market_type = DetectMarketType(instrument.c_str());
    mesh.base_currency = instrument.substr(0, dash_pos);
    if (mesh.market_type == MarketType::SWAP) {
        auto second_dash = instrument.find('-', dash_pos + 1);
        mesh.quote_currency = (second_dash != std::string::npos)
                                  ? instrument.substr(dash_pos + 1, second_dash - dash_pos - 1)
                                  : instrument.substr(dash_pos + 1);
    } else {
        mesh.quote_currency = instrument.substr(dash_pos + 1);
    }

    std::string position_side_str = config.get("position_side", "").asString();
    if (position_side_str.empty() || !ParsePosSide(position_side_str, mesh.position_side)) {
        ERROR("MultiMesh init failed, [REASON] missing or invalid 'position_side': '" + position_side_str +
              "', [INSTRUMENT] " + instrument);
        return;
    }
    if (mesh.market_type == MarketType::SWAP && mesh.position_side == PosSide::NET) {
        WARN("MultiMesh: SWAP instrument with position_side=net, [INSTRUMENT] " + instrument);
    }

    mesh.upper_price = config.get("upper_price", 0.0).asDouble();
    mesh.lower_price = config.get("lower_price", 0.0).asDouble();
    double grid_size = config.get("grid_size", 0.0).asDouble();
    mesh.active_grid_count = config.get("active_grid_count", 0).asInt();
    mesh.grid_volume = config.get("grid_volume", 0.0).asDouble();

    if (mesh.upper_price <= mesh.lower_price || grid_size <= 0 || mesh.grid_volume <= 0 ||
        mesh.active_grid_count <= 0) {
        ERROR("MultiMesh init failed, [REASON] invalid params, [INSTRUMENT] " + instrument + ", [UPPER] " +
              std::to_string(mesh.upper_price) + ", [LOWER] " + std::to_string(mesh.lower_price) + ", [GRID_SIZE] " +
              std::to_string(grid_size) + ", [ACTIVE_GRID_COUNT] " + std::to_string(mesh.active_grid_count));
        return;
    }

    mesh.grid_count = static_cast<int>(std::round((mesh.upper_price - mesh.lower_price) / grid_size));
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
         std::to_string(mesh.grid_count) + ", [ACTIVE_GRID_COUNT] " + std::to_string(mesh.active_grid_count) +
         ", [STEP] " + std::to_string(mesh.grid_step) + ", [GRID_VOLUME] " + std::to_string(mesh.grid_volume) +
         ", [NET_PROFIT_RATE] " + std::to_string((grid_profit_rate - round_trip_fee_rate) * 100) + "%");

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
            INFO("Cancel unmatched order: [ORDER_ID] " + std::string(report.order_id) + ", [SIDE] " +
                 ToString(report.side) + ", [PRICE] " + std::to_string(report.price));
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
                INFO("Adopt order: [INSTRUMENT] " + mesh->instrument + ", [SIDE] BUY, [PRICE] " +
                     std::to_string(report.price) + ", [ORDER_ID] " + std::string(report.order_id));
                return true;
            }
            if (report.side == Side::SELL) {
                if (mesh->grids[idx].state == GridState::EMPTY || mesh->grids[idx].state == GridState::BOUGHT) {
                    mesh->grids[idx].state = GridState::SELL_PENDING;
                    mesh->grids[idx].order_id = report.order_id;
                    if (idx > 0 && mesh->grids[idx - 1].state == GridState::EMPTY) {
                        mesh->grids[idx - 1].state = GridState::BOUGHT;
                    }
                    INFO("Adopt order: [INSTRUMENT] " + mesh->instrument + ", [SIDE] SELL, [PRICE] " +
                         std::to_string(report.price) + ", [ORDER_ID] " + std::string(report.order_id));
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

    double mid_price = (bbo.bid_price + bbo.ask_price) / 2.0;
    int new_mid = std::clamp(static_cast<int>((mid_price - mesh->lower_price) / mesh->grid_step), 0, mesh->grid_count);

    if (!mesh->initialized) {
        mesh->mid_grid_idx = new_mid;
        int lo = std::max(0, new_mid - mesh->active_grid_count + 1);
        int hi = std::min(mesh->grid_count, new_mid + mesh->active_grid_count);
        int buy_count = 0;
        int sell_count = 0;

        if (mesh->market_type == MarketType::SPOT) {
            double initial_quote_available = position_manager_->GetEffectiveAvailableSpot(mesh->quote_currency);
            double initial_base_available = position_manager_->GetEffectiveAvailableSpot(mesh->base_currency);

            for (int idx = new_mid; idx >= lo; --idx) {
                if (mesh->grids[idx].price >= mid_price - mesh->grid_step * 0.5)
                    continue;
                if (position_manager_->GetEffectiveAvailableSpot(mesh->quote_currency) <
                    mesh->grids[idx].price * mesh->grids[idx].volume)
                    break;
                PlaceBuyAtGrid(mesh, idx);
                ++buy_count;
            }
            for (int idx = new_mid + 1; idx <= hi; ++idx) {
                if (mesh->grids[idx].price < mid_price + mesh->grid_step * 0.5)
                    continue;
                PlaceSellAtGrid(mesh, idx);
                ++sell_count;
            }
            INFO("Place initial grid orders, [INSTRUMENT] " + mesh->instrument + ", [MID_PRICE] " +
                 std::to_string(mid_price) + ", [BUY_GRIDS] " + std::to_string(buy_count) + ", [SELL_GRIDS] " +
                 std::to_string(sell_count) + ", [QUOTE_USED] " +
                 std::to_string(initial_quote_available -
                                position_manager_->GetEffectiveAvailableSpot(mesh->quote_currency)) +
                 ", [BASE_USED] " +
                 std::to_string(initial_base_available -
                                position_manager_->GetEffectiveAvailableSpot(mesh->base_currency)));
        } else {
            for (int idx = new_mid; idx >= lo; --idx) {
                if (mesh->grids[idx].price >= mid_price - mesh->grid_step * 0.5)
                    continue;
                PlaceBuyAtGrid(mesh, idx);
                ++buy_count;
            }
            for (int idx = new_mid + 1; idx <= hi; ++idx) {
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
        return;
    }

    if (new_mid == mesh->mid_grid_idx) {
        return;
    }

    int old_mid = mesh->mid_grid_idx;
    mesh->mid_grid_idx = new_mid;

    int old_lo = std::max(0, old_mid - mesh->active_grid_count + 1);
    int old_hi = std::min(mesh->grid_count, old_mid + mesh->active_grid_count);
    int new_lo = std::max(0, new_mid - mesh->active_grid_count + 1);
    int new_hi = std::min(mesh->grid_count, new_mid + mesh->active_grid_count);

    if (new_mid > old_mid) {
        for (int idx = old_lo; idx < new_lo; ++idx) {
            if (mesh->grids[idx].state == GridState::BUY_PENDING && !mesh->grids[idx].order_id.empty()) {
                EmitCancel(mesh->instrument.c_str(), mesh->grids[idx].order_id.c_str(), mesh->market_type);
                mesh->grids[idx].state = GridState::CANCEL_PENDING;
            }
        }
        for (int idx = old_hi + 1; idx <= new_hi; ++idx) {
            PlaceSellAtGrid(mesh, idx);
        }
    } else {
        for (int idx = new_hi + 1; idx <= old_hi; ++idx) {
            if (mesh->grids[idx].state == GridState::SELL_PENDING && !mesh->grids[idx].order_id.empty()) {
                EmitCancel(mesh->instrument.c_str(), mesh->grids[idx].order_id.c_str(), mesh->market_type);
                mesh->grids[idx].state = GridState::CANCEL_PENDING;
            }
        }
        for (int idx = new_lo; idx < old_lo; ++idx) {
            PlaceBuyAtGrid(mesh, idx);
        }
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
            int lo = std::max(0, mesh->mid_grid_idx - mesh->active_grid_count + 1);
            int hi = std::min(mesh->grid_count, mesh->mid_grid_idx + mesh->active_grid_count);
            if (report.side == Side::BUY && grid_index < lo) {
                EmitCancel(mesh->instrument.c_str(), report.order_id, mesh->market_type);
                mesh->grids[grid_index].state = GridState::CANCEL_PENDING;
            } else if (report.side == Side::SELL && grid_index > hi) {
                EmitCancel(mesh->instrument.c_str(), report.order_id, mesh->market_type);
                mesh->grids[grid_index].state = GridState::CANCEL_PENDING;
            }
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
        if (grid.state == GridState::BUY_PENDING ||
            (grid.state == GridState::CANCEL_PENDING && report.side == Side::BUY)) {
            grid.state = GridState::BOUGHT;
            grid.order_id.clear();

            if (grid_index < mesh->grid_count) {
                PlaceSellAtGrid(mesh, grid_index + 1);
            }
        } else if (grid.state == GridState::SELL_PENDING ||
                   (grid.state == GridState::CANCEL_PENDING && report.side == Side::SELL)) {
            grid.state = GridState::EMPTY;
            grid.order_id.clear();

            int buy_grid = grid_index - 1;
            if (buy_grid >= 0) {
                if (mesh->grids[buy_grid].state == GridState::BOUGHT) {
                    mesh->grids[buy_grid].state = GridState::EMPTY;
                }
                PlaceBuyAtGrid(mesh, buy_grid);
            }
        }
    } else if (report.status == OrderStatus::CANCELLED) {
        if (grid.state == GridState::BUY_PENDING ||
            (grid.state == GridState::CANCEL_PENDING && report.side == Side::BUY)) {
            grid.state = GridState::EMPTY;
            grid.order_id.clear();
        } else if (grid.state == GridState::SELL_PENDING ||
                   (grid.state == GridState::CANCEL_PENDING && report.side == Side::SELL)) {
            grid.state = GridState::EMPTY;
            grid.order_id.clear();
        }
    } else if (report.status == OrderStatus::CANCEL_FAILED) {
        INFO("Cancel failed, likely already filled: [ORDER_ID] " + std::string(report.order_id) + ", [INSTRUMENT] " +
             mesh->instrument + ", [SIDE] " + ToString(report.side));
    } else if (report.status == OrderStatus::REJECTED) {
        if (grid.state == GridState::BUY_PENDING) {
            grid.state = GridState::EMPTY;
            grid.order_id.clear();
        } else if (grid.state == GridState::SELL_PENDING) {
            grid.state = GridState::EMPTY;
            grid.order_id.clear();
        } else if (grid.state == GridState::CANCEL_PENDING) {
            grid.state = GridState::EMPTY;
            grid.order_id.clear();
        }
    }
}

void MultiMeshStrategy::OnTimer() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_heartbeat_ts_ < std::chrono::minutes(15)) {
        return;
    }
    last_heartbeat_ts_ = now;

    for (auto& [instrument, mesh] : meshes_) {
        int buy_pending = 0, bought = 0, sell_pending = 0, cancel_pending = 0, empty = 0;
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
            case GridState::CANCEL_PENDING:
                ++cancel_pending;
                break;
            case GridState::EMPTY:
                ++empty;
                break;
            }
        }

        INFO("Heartbeat, [INSTRUMENT] " + instrument + ", [BID] " + std::to_string(mesh.last_bid) + ", [ASK] " +
             std::to_string(mesh.last_ask) + ", [BUY_PENDING] " + std::to_string(buy_pending) + ", [BOUGHT] " +
             std::to_string(bought) + ", [SELL_PENDING] " + std::to_string(sell_pending) + ", [CANCEL_PENDING] " +
             std::to_string(cancel_pending) + ", [EMPTY] " + std::to_string(empty));
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
    if (mesh->market_type == MarketType::SPOT) {
        if (position_manager_->GetEffectiveAvailableSpot(mesh->quote_currency) < grid.price * grid.volume) {
            return;
        }
    }

    grid.state = GridState::BUY_PENDING;
    grid.order_id.clear();
    EmitBuy(mesh->instrument.c_str(), OrderType::LIMIT, grid.price, grid.volume, mesh->market_type,
            mesh->position_side);

    INFO("Place order: [INSTRUMENT] " + mesh->instrument + ", [SIDE] BUY, [PRICE] " + std::to_string(grid.price) +
         ", [VOLUME] " + std::to_string(grid.volume) + ", [EFFECTIVE_AVAILABLE] " +
         std::to_string(position_manager_->GetEffectiveAvailableSpot(mesh->quote_currency)));
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
        if (position_manager_->GetEffectiveAvailableSpot(mesh->base_currency) < grid.volume) {
            return;
        }
    } else {
        if (position_manager_->GetSwapPosition(mesh->instrument, mesh->position_side).contracts < grid.volume) {
            return;
        }
    }

    grid.state = GridState::SELL_PENDING;
    grid.order_id.clear();
    EmitSell(mesh->instrument.c_str(), OrderType::LIMIT, grid.price, grid.volume, mesh->market_type,
             mesh->position_side);

    INFO("Place order: [INSTRUMENT] " + mesh->instrument + ", [SIDE] SELL, [PRICE] " + std::to_string(grid.price) +
         ", [VOLUME] " + std::to_string(grid.volume) + ", [EFFECTIVE_AVAILABLE] " +
         std::to_string(position_manager_->GetEffectiveAvailableSpot(mesh->base_currency)));
}
