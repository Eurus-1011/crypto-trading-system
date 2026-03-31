#include "strategy.hpp"

#include "common/utils.hpp"

#include <cstring>

static std::string ExtractBase(const char* instrument) {
    const char* dash = std::strchr(instrument, '-');
    return dash ? std::string(instrument, dash) : instrument;
}

static std::string ExtractQuote(const char* instrument) {
    const char* dash = std::strchr(instrument, '-');
    return dash ? std::string(dash + 1) : instrument;
}

void Strategy::EmitBuy(const char* instrument, OrderType order_type, double price, double volume,
                       MarketType market_type, PosSide position_side) {
    if (market_type == MarketType::SPOT) {
        position_manager_->ReserveSpot(ExtractQuote(instrument), price * volume);
    }
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
    if (market_type == MarketType::SPOT) {
        position_manager_->ReserveSpot(ExtractBase(instrument), volume);
    }
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
