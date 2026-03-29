#pragma once

#include "ring_shm.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

enum class MarketType : int8_t { SPOT, SWAP };
enum class Side : int8_t { BUY, SELL };
enum class PosSide : int8_t { NET, LONG, SHORT };
enum class Action : int8_t { BUY, SELL, CANCEL };
enum class OrderType : int8_t { MARKET, LIMIT };
enum class OrderStatus : int8_t { NEW, FILLED, PARTIALLY_FILLED, CANCELLED, CANCEL_FAILED, REJECTED };

inline const char* ToString(MarketType v) {
    switch (v) {
    case MarketType::SPOT:
        return "SPOT";
    case MarketType::SWAP:
        return "SWAP";
    }
    std::unreachable();
}

inline const char* ToString(Side v) {
    switch (v) {
    case Side::BUY:
        return "BUY";
    case Side::SELL:
        return "SELL";
    }
    std::unreachable();
}

inline const char* ToString(PosSide v) {
    switch (v) {
    case PosSide::NET:
        return "NET";
    case PosSide::LONG:
        return "LONG";
    case PosSide::SHORT:
        return "SHORT";
    }
    std::unreachable();
}

inline const char* ToString(Action v) {
    switch (v) {
    case Action::BUY:
        return "BUY";
    case Action::SELL:
        return "SELL";
    case Action::CANCEL:
        return "CANCEL";
    }
    std::unreachable();
}

inline const char* ToString(OrderType v) {
    switch (v) {
    case OrderType::MARKET:
        return "MARKET";
    case OrderType::LIMIT:
        return "LIMIT";
    }
    std::unreachable();
}

inline const char* ToString(OrderStatus v) {
    switch (v) {
    case OrderStatus::NEW:
        return "NEW";
    case OrderStatus::FILLED:
        return "FILLED";
    case OrderStatus::PARTIALLY_FILLED:
        return "PARTIALLY_FILLED";
    case OrderStatus::CANCELLED:
        return "CANCELLED";
    case OrderStatus::CANCEL_FAILED:
        return "CANCEL_FAILED";
    case OrderStatus::REJECTED:
        return "REJECTED";
    }
    std::unreachable();
}

inline MarketType DetectMarketType(const char* instrument) {
    static constexpr const char* kSwapSuffix = "-SWAP";
    static constexpr size_t kSwapSuffixLen = 5;
    size_t len = std::strlen(instrument);
    if (len > kSwapSuffixLen && std::strcmp(instrument + len - kSwapSuffixLen, kSwapSuffix) == 0) {
        return MarketType::SWAP;
    }
    return MarketType::SPOT;
}

static constexpr int MAX_DEPTH_LEVELS = 5;

struct alignas(64) Ticker {
    uint64_t exchange_ts_ms;
    uint64_t local_ts_ns;
    char instrument[32];
    MarketType market_type;
    double last_price;
    double last_volume;
    double bid_price;
    double bid_volume;
    double ask_price;
    double ask_volume;
    double open_24h;
    double high_24h;
    double low_24h;
    double volume_24h;
    double volume_currency_24h;

    void SetInstrument(const char* src) {
        std::strncpy(instrument, src, sizeof(instrument) - 1);
        instrument[sizeof(instrument) - 1] = '\0';
    }
};

struct alignas(64) BBO {
    uint64_t exchange_ts_ms;
    uint64_t local_ts_ns;
    char instrument[32];
    MarketType market_type;
    double bid_price;
    double bid_volume;
    double ask_price;
    double ask_volume;

    void SetInstrument(const char* src) {
        std::strncpy(instrument, src, sizeof(instrument) - 1);
        instrument[sizeof(instrument) - 1] = '\0';
    }
};

struct PriceLevel {
    double price;
    double volume;
    int order_count;
};

struct alignas(64) Depth {
    uint64_t exchange_ts_ms;
    uint64_t local_ts_ns;
    char instrument[32];
    MarketType market_type;
    int ask_levels;
    int bid_levels;
    PriceLevel asks[MAX_DEPTH_LEVELS];
    PriceLevel bids[MAX_DEPTH_LEVELS];

    void SetInstrument(const char* src) {
        std::strncpy(instrument, src, sizeof(instrument) - 1);
        instrument[sizeof(instrument) - 1] = '\0';
    }
};

struct alignas(64) Trade {
    uint64_t exchange_ts_ms;
    uint64_t local_ts_ns;
    char instrument[32];
    char trade_id[32];
    MarketType market_type;
    Side side;
    double price;
    double volume;

    void SetInstrument(const char* src) {
        std::strncpy(instrument, src, sizeof(instrument) - 1);
        instrument[sizeof(instrument) - 1] = '\0';
    }
};

static constexpr const char* SHM_TICKER = "/cts_ticker";
static constexpr const char* SHM_BBO = "/cts_bbo";
static constexpr const char* SHM_DEPTH = "/cts_depth";
static constexpr const char* SHM_TRADE = "/cts_trade";

using TickerRing = RingShm<Ticker, 4096>;
using BBORing = RingShm<BBO, 8192>;
using DepthRing = RingShm<Depth, 2048>;
using TradeRing = RingShm<Trade, 8192>;

struct alignas(64) Signal {
    uint64_t timestamp_ns;
    char instrument[32];
    char order_id[32];
    Action action;
    OrderType order_type;
    MarketType market_type;
    PosSide position_side;
    double price;
    double volume;

    void SetInstrument(const char* src) {
        std::strncpy(instrument, src, sizeof(instrument) - 1);
        instrument[sizeof(instrument) - 1] = '\0';
    }

    void SetOrderId(const char* src) {
        std::strncpy(order_id, src, sizeof(order_id) - 1);
        order_id[sizeof(order_id) - 1] = '\0';
    }
};

struct alignas(64) ExecutionReport {
    uint64_t timestamp_ns;
    char instrument[32];
    char order_id[32];
    OrderStatus status;
    Side side;
    MarketType market_type;
    PosSide position_side;
    double price;
    double filled_volume;
    double total_volume;
    double avg_fill_price;
    double fee;
    char fee_currency[16];

    void SetInstrument(const char* src) {
        std::strncpy(instrument, src, sizeof(instrument) - 1);
        instrument[sizeof(instrument) - 1] = '\0';
    }

    void SetOrderId(const char* src) {
        std::strncpy(order_id, src, sizeof(order_id) - 1);
        order_id[sizeof(order_id) - 1] = '\0';
    }

    void SetFeeCurrency(const char* src) {
        std::strncpy(fee_currency, src, sizeof(fee_currency) - 1);
        fee_currency[sizeof(fee_currency) - 1] = '\0';
    }
};

struct OrderRequest {
    std::string instrument;
    Side side;
    OrderType order_type;
    MarketType market_type;
    PosSide position_side;
    std::string size;
    std::string price;
    std::string target_currency;
};

static constexpr const char* SHM_SIGNAL = "/cts_signal";
static constexpr const char* SHM_EXECUTION_REPORT = "/cts_exec_report";

using SignalRing = RingShm<Signal, 1024>;
using ExecutionReportRing = RingShm<ExecutionReport, 4096>;

struct SwapPosition {
    std::string instrument;
    PosSide position_side;
    double contracts = 0.0;
    double average_opening_price = 0.0;
    double unrealized_profit_loss = 0.0;
};
