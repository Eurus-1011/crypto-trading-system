#include "multi_mesh.hpp"

#include "common/logger.hpp"
#include "common/utils.hpp"
#include "strategy_engine/strategy_registry.hpp"

#include <cmath>

static bool ParsePosSide(std::string_view s, PosSide& out) {
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

void MultiMeshStrategy::Init(std::string_view params_json) {
    if (params_json.empty()) {
        ERROR("MultiMesh init failed: [REASON] empty params");
        Stop();
        return;
    }

    simdjson::dom::parser parser;
    simdjson::padded_string padded(params_json);
    auto doc = parser.parse(padded);
    if (doc.error()) {
        ERROR("MultiMesh init failed: [REASON] invalid params JSON");
        Stop();
        return;
    }

    auto mesh_array = doc["mesh"].get_array();
    if (mesh_array.error()) {
        ERROR("MultiMesh init failed: [REASON] missing or invalid 'mesh' array");
        Stop();
        return;
    }

    double fee_rate_val;
    fee_rate_ = doc["fee_rate"].get(fee_rate_val) ? 0.001 : fee_rate_val;

    for (auto mesh_element : mesh_array.value()) {
        InitMesh(mesh_element, fee_rate_);
    }

    if (meshes_.empty()) {
        ERROR("MultiMesh init failed: [REASON] no valid mesh configured");
        Stop();
        return;
    }

    INFO("MultiMesh init success: [MESH_COUNT] " + std::to_string(meshes_.size()));
    last_heartbeat_ts_ = std::chrono::steady_clock::now();
}

void MultiMeshStrategy::InitMesh(simdjson::dom::element config, double fee_rate) {
    std::string_view instrument_view;
    if (config["instrument"].get(instrument_view) || instrument_view.empty()) {
        return;
    }
    std::string instrument(instrument_view);

    auto dash_pos = instrument.find('-');
    if (dash_pos == std::string::npos) {
        ERROR("MultiMesh init failed: [REASON] invalid instrument format, [INSTRUMENT] " + instrument);
        return;
    }

    const auto* info = InstrumentRegistry::Instance().Find(instrument.c_str());
    if (!info) {
        ERROR("MultiMesh init failed: [REASON] instrument info not found, [INSTRUMENT] " + instrument);
        return;
    }

    MeshConfig mesh;
    mesh.instrument = instrument;
    mesh.market_type = DetectMarketType(instrument.c_str());
    mesh.base_currency = instrument.substr(0, dash_pos);
    mesh.price_precision = info->price_precision;
    mesh.volume_precision = info->volume_precision;
    if (mesh.market_type == MarketType::SWAP) {
        auto second_dash = instrument.find('-', dash_pos + 1);
        mesh.quote_currency = (second_dash != std::string::npos)
                                  ? instrument.substr(dash_pos + 1, second_dash - dash_pos - 1)
                                  : instrument.substr(dash_pos + 1);
    } else {
        mesh.quote_currency = instrument.substr(dash_pos + 1);
    }

    std::string_view position_side_str;
    (void)config["position_side"].get(position_side_str);
    if (position_side_str.empty() || !ParsePosSide(position_side_str, mesh.position_side)) {
        ERROR("MultiMesh init failed: [REASON] missing or invalid 'position_side': '" + std::string(position_side_str) +
              "', [INSTRUMENT] " + instrument);
        return;
    }
    if (mesh.market_type == MarketType::SWAP && mesh.position_side == PosSide::NET) {
        WARN("MultiMesh: SWAP instrument with position_side=net, [INSTRUMENT] " + instrument);
    }

    double upper_price, lower_price, grid_size, grid_volume;
    int64_t active_grid_count;
    upper_price = config["upper_price"].get(upper_price) ? 0.0 : upper_price;
    lower_price = config["lower_price"].get(lower_price) ? 0.0 : lower_price;
    grid_size = config["grid_size"].get(grid_size) ? 0.0 : grid_size;
    mesh.active_grid_count =
        config["active_grid_count"].get(active_grid_count) ? 0 : static_cast<int>(active_grid_count);
    grid_volume = config["grid_volume"].get(grid_volume) ? 0.0 : grid_volume;

    if (upper_price <= lower_price || grid_size <= 0 || grid_volume <= 0 || mesh.active_grid_count <= 0) {
        ERROR("MultiMesh init failed: [REASON] invalid params, [INSTRUMENT] " + instrument + ", [UPPER] " +
              std::to_string(upper_price) + ", [LOWER] " + std::to_string(lower_price) + ", [GRID_SIZE] " +
              std::to_string(grid_size) + ", [ACTIVE_GRID_COUNT] " + std::to_string(mesh.active_grid_count));
        return;
    }

    Price upper_scaled = Encode(upper_price, mesh.price_precision);
    Price lower_scaled = Encode(lower_price, mesh.price_precision);
    Price grid_size_scaled = Encode(grid_size, mesh.price_precision);

    mesh.upper_price = upper_scaled;
    mesh.lower_price = lower_scaled;
    mesh.grid_count = static_cast<int>((upper_scaled - lower_scaled) / grid_size_scaled);
    mesh.grid_step = grid_size_scaled;
    mesh.grid_volume = Encode(grid_volume, mesh.volume_precision);

    double mid_price = (upper_price + lower_price) / 2.0;
    double grid_profit_rate = grid_size / mid_price;
    double round_trip_fee_rate = fee_rate * 2.0;

    if (grid_profit_rate <= round_trip_fee_rate) {
        ERROR("MultiMesh init failed: [REASON] grid profit rate below fee, [INSTRUMENT] " + instrument +
              ", [GRID_PROFIT] " + std::to_string(grid_profit_rate * 100) + "%, [ROUND_TRIP_FEE] " +
              std::to_string(round_trip_fee_rate * 100) + "%");
        return;
    }

    mesh.grids.resize(mesh.grid_count + 1);
    for (int idx = 0; idx <= mesh.grid_count; ++idx) {
        mesh.grids[idx].price = lower_scaled + idx * mesh.grid_step;
        mesh.grids[idx].volume = mesh.grid_volume;
    }

    INFO("MultiMesh instrument init success: [INSTRUMENT] " + instrument + ", [RANGE] " +
         Format(mesh.lower_price, mesh.price_precision) + " ~ " + Format(mesh.upper_price, mesh.price_precision) +
         ", [ACTIVE_GRID_COUNT] " + std::to_string(mesh.active_grid_count) + ", [STEP] " +
         Format(mesh.grid_step, mesh.price_precision) + ", [GRID_VOLUME] " +
         Format(mesh.grid_volume, mesh.volume_precision) + ", [NET_PROFIT_RATE] " +
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
            INFO("Cancel unmatched order: [ORDER_ID] " + std::string(report.order_id) + ", [SIDE] " +
                 ToString(report.side) + ", [PRICE] " + Format(report.price, mesh->price_precision));
        }
    }

    INFO("Reconstruct from pending orders: [ADOPTED] " + std::to_string(adopted) + ", [CANCELLED] " +
         std::to_string(cancelled));
}

bool MultiMeshStrategy::TryAdoptOrder(MeshConfig* mesh, const ExecutionReport& report) {
    for (int idx = 0; idx <= mesh->grid_count; ++idx) {
        if (mesh->grids[idx].price == report.price) {
            if (mesh->grids[idx].state == GridState::EMPTY) {
                mesh->grids[idx].state = (report.side == Side::BUY) ? GridState::BUY_PENDING : GridState::SELL_PENDING;
                mesh->grids[idx].order_id = report.order_id;
                INFO("Adopt order: [INSTRUMENT] " + mesh->instrument + ", [SIDE] " + ToString(report.side) +
                     ", [PRICE] " + Format(report.price, mesh->price_precision) + ", [ORDER_ID] " +
                     std::string(report.order_id));
                return true;
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

    if (mesh->center_grid_idx >= 0) {
        int new_center = mesh->center_grid_idx;

        while (new_center + 1 <= mesh->grid_count && mesh->grids[new_center + 1].state == GridState::EMPTY &&
               bbo.bid_price >= mesh->grids[new_center + 1].price) {
            ++new_center;
        }

        if (new_center == mesh->center_grid_idx) {
            while (new_center - 1 >= 0 && mesh->grids[new_center - 1].state == GridState::EMPTY &&
                   bbo.ask_price <= mesh->grids[new_center - 1].price) {
                --new_center;
            }
        }

        if (new_center != mesh->center_grid_idx) {
            mesh->center_grid_idx = new_center;
            Rebalance(mesh);
        }

        return;
    }

    Price mid_scaled = (bbo.bid_price + bbo.ask_price) / 2;
    mesh->center_grid_idx =
        std::clamp(static_cast<int>((mid_scaled - mesh->lower_price + mesh->grid_step / 2) / mesh->grid_step), 0,
                   mesh->grid_count);

    INFO("First BBO: [INSTRUMENT] " + mesh->instrument + ", [MID_PRICE] " + Format(mid_scaled, mesh->price_precision) +
         ", [CENTER_PRICE] " + Format(mesh->grids[mesh->center_grid_idx].price, mesh->price_precision));
    Rebalance(mesh);
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
            mesh->center_grid_idx = grid_index;
            grid.state = GridState::EMPTY;
            grid.order_id.clear();
            INFO("Buy filled: [INSTRUMENT] " + mesh->instrument + ", [PRICE] " +
                 Format(grid.price, mesh->price_precision) + ", [VOLUME] " +
                 Format(grid.volume, mesh->volume_precision) + ", [CENTER_PRICE] " +
                 Format(grid.price, mesh->price_precision));
            Rebalance(mesh);
        } else if (grid.state == GridState::SELL_PENDING ||
                   (grid.state == GridState::CANCEL_PENDING && report.side == Side::SELL)) {
            mesh->center_grid_idx = grid_index;
            grid.state = GridState::EMPTY;
            grid.order_id.clear();
            INFO("Sell filled: [INSTRUMENT] " + mesh->instrument + ", [PRICE] " +
                 Format(grid.price, mesh->price_precision) + ", [VOLUME] " +
                 Format(grid.volume, mesh->volume_precision) + ", [CENTER_PRICE] " +
                 Format(grid.price, mesh->price_precision));
            Rebalance(mesh);
        }
    } else if (report.status == OrderStatus::CANCELLED) {
        grid.state = GridState::EMPTY;
        grid.order_id.clear();
        if (mesh->center_grid_idx >= 0) {
            int lo = std::max(0, mesh->center_grid_idx - mesh->active_grid_count);
            int hi = std::min(mesh->grid_count, mesh->center_grid_idx + mesh->active_grid_count);
            if (grid_index >= lo && grid_index <= hi && grid_index != mesh->center_grid_idx) {
                if (grid_index < mesh->center_grid_idx) {
                    PlaceBuyAtGrid(mesh, grid_index);
                } else {
                    PlaceSellAtGrid(mesh, grid_index);
                }
            }
        }
    } else if (report.status == OrderStatus::CANCEL_FAILED) {
        INFO("Cancel failed, likely already filled: [ORDER_ID] " + std::string(report.order_id) + ", [INSTRUMENT] " +
             mesh->instrument + ", [SIDE] " + ToString(report.side));
    } else if (report.status == OrderStatus::REJECTED) {
        grid.state = GridState::EMPTY;
        grid.order_id.clear();
        ERROR("Order rejected: [ORDER_ID] " + std::string(report.order_id) + ", [INSTRUMENT] " + mesh->instrument +
              ", [SIDE] " + ToString(report.side) + ", [PRICE] " + Format(grid.price, mesh->price_precision));
    }
}

void MultiMeshStrategy::Rebalance(MeshConfig* mesh) {
    int lo = std::max(0, mesh->center_grid_idx - mesh->active_grid_count);
    int hi = std::min(mesh->grid_count, mesh->center_grid_idx + mesh->active_grid_count);

    for (int idx = 0; idx <= mesh->grid_count; ++idx) {
        if (idx == mesh->center_grid_idx) {
            auto& center_grid = mesh->grids[idx];
            if ((center_grid.state == GridState::BUY_PENDING || center_grid.state == GridState::SELL_PENDING) &&
                !center_grid.order_id.empty()) {
                EmitCancel(mesh->instrument.c_str(), center_grid.order_id.c_str(), mesh->market_type);
                center_grid.state = GridState::CANCEL_PENDING;
            }
            continue;
        }
        auto& grid = mesh->grids[idx];
        bool in_window = (idx >= lo && idx <= hi);

        if (!in_window) {
            if ((grid.state == GridState::BUY_PENDING || grid.state == GridState::SELL_PENDING) &&
                !grid.order_id.empty()) {
                EmitCancel(mesh->instrument.c_str(), grid.order_id.c_str(), mesh->market_type);
                grid.state = GridState::CANCEL_PENDING;
            }
        } else if (idx < mesh->center_grid_idx) {
            if (grid.state == GridState::EMPTY) {
                PlaceBuyAtGrid(mesh, idx);
            } else if (grid.state == GridState::SELL_PENDING && !grid.order_id.empty()) {
                EmitCancel(mesh->instrument.c_str(), grid.order_id.c_str(), mesh->market_type);
                grid.state = GridState::CANCEL_PENDING;
            }
        } else {
            if (grid.state == GridState::EMPTY) {
                PlaceSellAtGrid(mesh, idx);
            } else if (grid.state == GridState::BUY_PENDING && !grid.order_id.empty()) {
                EmitCancel(mesh->instrument.c_str(), grid.order_id.c_str(), mesh->market_type);
                grid.state = GridState::CANCEL_PENDING;
            }
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
        int buy_pending = 0, sell_pending = 0, cancel_pending = 0, empty = 0;
        for (int idx = 0; idx <= mesh.grid_count; ++idx) {
            switch (mesh.grids[idx].state) {
            case GridState::BUY_PENDING:
                ++buy_pending;
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

        INFO(
            "Heartbeat: [INSTRUMENT] " + instrument + ", [BID] " + Format(mesh.last_bid, mesh.price_precision) +
            ", [ASK] " + Format(mesh.last_ask, mesh.price_precision) + ", [CENTER_PRICE] " +
            (mesh.center_grid_idx >= 0 ? Format(mesh.grids[mesh.center_grid_idx].price, mesh.price_precision) : "N/A") +
            ", [BUY_PENDING] " + std::to_string(buy_pending) + ", [SELL_PENDING] " + std::to_string(sell_pending) +
            ", [CANCEL_PENDING] " + std::to_string(cancel_pending));
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

int MultiMeshStrategy::FindGridByPriceAndState(MeshConfig* mesh, Price price, GridState expected_state) const {
    for (int idx = 0; idx <= mesh->grid_count; ++idx) {
        if (mesh->grids[idx].state == expected_state && mesh->grids[idx].price == price) {
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
        double cost = Decode(grid.price, mesh->price_precision) * Decode(grid.volume, mesh->volume_precision);
        if (position_manager_->GetEffectiveAvailableSpot(mesh->quote_currency) < cost) {
            return;
        }
    }

    grid.state = GridState::BUY_PENDING;
    grid.order_id.clear();
    EmitBuy(mesh->instrument.c_str(), OrderType::LIMIT, grid.price, grid.volume, mesh->market_type, mesh->position_side,
            TradeMode::CASH);

    INFO("Place order: [INSTRUMENT] " + mesh->instrument + ", [SIDE] BUY, [PRICE] " +
         Format(grid.price, mesh->price_precision) + ", [VOLUME] " + Format(grid.volume, mesh->volume_precision) +
         ", [EFFECTIVE_AVAILABLE] " +
         std::to_string(position_manager_->GetEffectiveAvailableSpot(mesh->quote_currency)));
}

void MultiMeshStrategy::PlaceSellAtGrid(MeshConfig* mesh, int grid_index) {
    if (grid_index < 0 || grid_index > mesh->grid_count) {
        return;
    }
    auto& grid = mesh->grids[grid_index];
    if (grid.state != GridState::EMPTY) {
        return;
    }
    if (mesh->market_type == MarketType::SPOT) {
        double volume = Decode(grid.volume, mesh->volume_precision);
        if (position_manager_->GetEffectiveAvailableSpot(mesh->base_currency) < volume) {
            return;
        }
    } else {
        double volume = Decode(grid.volume, mesh->volume_precision);
        if (position_manager_->GetSwapPosition(mesh->instrument, mesh->position_side).contracts < volume) {
            return;
        }
    }

    grid.state = GridState::SELL_PENDING;
    grid.order_id.clear();
    EmitSell(mesh->instrument.c_str(), OrderType::LIMIT, grid.price, grid.volume, mesh->market_type,
             mesh->position_side, TradeMode::CASH);

    INFO("Place order: [INSTRUMENT] " + mesh->instrument + ", [SIDE] SELL, [PRICE] " +
         Format(grid.price, mesh->price_precision) + ", [VOLUME] " + Format(grid.volume, mesh->volume_precision) +
         ", [EFFECTIVE_AVAILABLE] " +
         (mesh->market_type == MarketType::SPOT
              ? std::to_string(position_manager_->GetEffectiveAvailableSpot(mesh->base_currency))
              : std::to_string(position_manager_->GetSwapPosition(mesh->instrument, mesh->position_side).contracts)));
}
