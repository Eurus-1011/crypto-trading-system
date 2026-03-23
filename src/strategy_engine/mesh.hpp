#pragma once

#include "strategy_engine/strategy.hpp"

#include <string>
#include <vector>

enum class GridState : int8_t { EMPTY, BUY_PENDING, BOUGHT, SELL_PENDING };

struct GridLevel {
    double price;
    double volume;
    GridState state = GridState::EMPTY;
    std::string order_id;
    uint64_t order_sent_ts_ns = 0;
    double buy_fill_price = 0.0;
};

class MeshStrategy : public Strategy {
  public:
    void Init(const Json::Value& params) override;
    void Reconstruct(const std::vector<ExecutionReport>& pending_orders) override;
    void OnBBO(const BBO& bbo) override;
    void OnExecutionReport(const ExecutionReport& report) override;
    void OnTimer() override;

  private:
    int FindGridByOrderId(const std::string& order_id) const;
    int FindGridByPriceAndState(double price, GridState expected_state) const;
    void PlaceBuyAtGrid(int grid_index);
    void PlaceSellAtGrid(int grid_index);
    bool TryAdoptOrder(const ExecutionReport& report);

    std::string instrument_;
    double upper_price_ = 0.0;
    double lower_price_ = 0.0;
    int grid_count_ = 0;
    double grid_step_ = 0.0;
    double total_investment_ = 0.0;
    double fee_rate_ = 0.001;

    std::vector<GridLevel> grids_;
    bool initialized_ = false;
    double total_profit_ = 0.0;
    double total_fee_ = 0.0;
    int total_round_trips_ = 0;
};
