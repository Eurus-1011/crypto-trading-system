#pragma once

#include "common/logger.hpp"
#include "strategy_engine/strategy.hpp"

#include <chrono>
#include <cmath>
#include <map>

enum class GridState : int8_t { EMPTY, BUY_PENDING, BOUGHT, SELL_PENDING };

struct GridLevel {
    double price;
    double volume;
    GridState state = GridState::EMPTY;
    std::string order_id;
    uint64_t order_sent_ts_ns = 0;
    double buy_fill_price = 0.0;
};

struct MeshConfig {
    std::string instrument;
    std::string base_currency;
    std::string quote_currency;
    double upper_price = 0.0;
    double lower_price = 0.0;
    int grid_count = 0;
    double grid_step = 0.0;
    double grid_volume = 0.0;

    std::vector<GridLevel> grids;
    bool initialized = false;

    double total_profit = 0.0;
    double total_fee = 0.0;
    int total_round_trips = 0;

    double last_bid = 0.0;
    double last_ask = 0.0;
    uint64_t last_bbo_ts_ns = 0;
    std::chrono::steady_clock::time_point last_heartbeat_ts{};
};

class MultiMeshStrategy : public Strategy {
  public:
    void Init(const Json::Value& params) override;
    void Reconstruct(const std::vector<ExecutionReport>& pending_orders) override;
    void OnBBO(const BBO& bbo) override;
    void OnExecutionReport(const ExecutionReport& report) override;
    void OnTimer() override;

  private:
    MeshConfig* FindMesh(const char* instrument);
    const MeshConfig* FindMesh(const char* instrument) const;
    int FindGridByOrderId(MeshConfig* mesh, const std::string& order_id) const;
    int FindGridByPriceAndState(MeshConfig* mesh, double price, GridState expected_state) const;
    bool TryAdoptOrder(MeshConfig* mesh, const ExecutionReport& report);
    void PlaceBuyAtGrid(MeshConfig* mesh, int grid_index);
    void PlaceSellAtGrid(MeshConfig* mesh, int grid_index);
    void InitMesh(const Json::Value& config, double fee_rate);

    std::map<std::string, MeshConfig> meshes_;
    double fee_rate_ = 0.001;
    std::chrono::steady_clock::time_point last_heartbeat_ts_{};
};
