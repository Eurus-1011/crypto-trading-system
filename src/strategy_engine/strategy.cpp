#include "strategy.hpp"

void Strategy::EmitBuy(const char* instrument, OrderType order_type, double price, double volume) {
    Signal signal{};
    signal.timestamp_ns = NowNs();
    signal.SetInstrument(instrument);
    signal.action = Action::BUY;
    signal.order_type = order_type;
    signal.price = price;
    signal.volume = volume;
    shm_push(signal_ring_, signal);
}

void Strategy::EmitSell(const char* instrument, OrderType order_type, double price, double volume) {
    Signal signal{};
    signal.timestamp_ns = NowNs();
    signal.SetInstrument(instrument);
    signal.action = Action::SELL;
    signal.order_type = order_type;
    signal.price = price;
    signal.volume = volume;
    shm_push(signal_ring_, signal);
}

void Strategy::EmitCancel(const char* instrument, const char* order_id) {
    Signal signal{};
    signal.timestamp_ns = NowNs();
    signal.SetInstrument(instrument);
    signal.SetOrderId(order_id);
    signal.action = Action::CANCEL;
    shm_push(signal_ring_, signal);
}
