#pragma once

#include "strategy_engine/strategy.hpp"

#include <chrono>
#include <map>
#include <simdjson.h>
#include <string_view>

enum class GridState : int8_t { EMPTY, BUY_PENDING, SELL_PENDING, CANCEL_PENDING };

struct GridLevel {
    Price price;
    Volume volume;
    GridState state = GridState::EMPTY;
    std::string order_id;
};

struct MeshConfig {
    std::string instrument;
    std::string base_currency;
    std::string quote_currency;
    MarketType market_type;
    PosSide position_side;
    int price_precision = 0;
    int volume_precision = 0;
    Price upper_price = 0;
    Price lower_price = 0;
    int grid_count = 0;
    int active_grid_count = 0;
    Price grid_step = 0;
    Volume grid_volume = 0;

    std::vector<GridLevel> grids;
    int center_grid_idx = -1;

    Price last_bid = 0;
    Price last_ask = 0;
};

class MultiMeshStrategy : public Strategy {
  public:
    void Init(std::string_view params_json) override;
    void Reconstruct(const std::vector<ExecutionReport>& pending_orders) override;
    void OnBBO(const BBO& bbo) override;
    void OnExecutionReport(const ExecutionReport& report) override;
    void OnTimer() override;

  private:
    MeshConfig* FindMesh(const char* instrument);
    const MeshConfig* FindMesh(const char* instrument) const;
    int FindGridByOrderId(MeshConfig* mesh, const std::string& order_id) const;
    int FindGridByPriceAndState(MeshConfig* mesh, Price price, GridState expected_state) const;
    bool TryAdoptOrder(MeshConfig* mesh, const ExecutionReport& report);
    void PlaceBuyAtGrid(MeshConfig* mesh, int grid_index);
    void PlaceSellAtGrid(MeshConfig* mesh, int grid_index);
    void Rebalance(MeshConfig* mesh);
    void InitMesh(simdjson::dom::element config, double fee_rate);

    std::map<std::string, MeshConfig> meshes_;
    double fee_rate_ = 0.001;
    std::chrono::steady_clock::time_point last_heartbeat_ts_{};
};
