#include "strategy.hpp"

void Strategy::EmitBuy(const char* instrument, OrderType order_type, double price, double volume,
                       MarketType market_type, PosSide position_side) {
    Signal signal{};
    signal.timestamp_ns = NowNs();
    signal.SetInstrument(instrument);
    signal.action = Action::BUY;
    signal.order_type = order_type;
    signal.market_type = market_type;
    signal.position_side = position_side;
    signal.price = price;
    signal.volume = volume;
    shm_push(signal_ring_, signal);
}

void Strategy::EmitSell(const char* instrument, OrderType order_type, double price, double volume,
                        MarketType market_type, PosSide position_side) {
    Signal signal{};
    signal.timestamp_ns = NowNs();
    signal.SetInstrument(instrument);
    signal.action = Action::SELL;
    signal.order_type = order_type;
    signal.market_type = market_type;
    signal.position_side = position_side;
    signal.price = price;
    signal.volume = volume;
    shm_push(signal_ring_, signal);
}

void Strategy::EmitCancel(const char* instrument, const char* order_id, MarketType market_type) {
    Signal signal{};
    signal.timestamp_ns = NowNs();
    signal.SetInstrument(instrument);
    signal.SetOrderId(order_id);
    signal.action = Action::CANCEL;
    signal.market_type = market_type;
    shm_push(signal_ring_, signal);
}
