#pragma once

#include "ring_shm.hpp"

#include <cstdint>
#include <cstring>

static constexpr int MAX_DEPTH_LEVELS = 5;

enum class TradeSide : int8_t { BUY = 1, SELL = -1 };

struct alignas(64) Ticker {
    uint64_t exchange_ts_ms;
    uint64_t local_ts_ns;
    char instrument[32];
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
    double price;
    double volume;
    TradeSide side;

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
