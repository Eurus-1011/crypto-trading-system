#pragma once

#include "ring_shm.hpp"
#include <cstdint>
#include <cstring>
#include <time.h>

namespace cts {

inline uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

inline uint64_t wall_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

struct alignas(64) MarketData {
    uint64_t exchange_ts_ms;
    uint64_t local_ts_ns;
    char instrument[32];
    double last_price;
    double best_bid;
    double best_ask;
    double bid_size;
    double ask_size;
    double volume_24h;

    void set_instrument(const char* s) {
        std::strncpy(instrument, s, sizeof(instrument) - 1);
        instrument[sizeof(instrument) - 1] = '\0';
    }
};

enum class Side : int8_t { BUY = 1, SELL = -1 };
enum class OrderType : int8_t { MARKET = 0, LIMIT = 1 };

struct alignas(64) Signal {
    uint64_t timestamp_ns;
    char instrument[32];
    Side side;
    OrderType order_type;
    double price;
    double quantity;

    void set_instrument(const char* s) {
        std::strncpy(instrument, s, sizeof(instrument) - 1);
        instrument[sizeof(instrument) - 1] = '\0';
    }
};

static constexpr size_t MARKET_DATA_SHM_CAPACITY = 4096;
static constexpr size_t SIGNAL_SHM_CAPACITY = 1024;

using MarketDataRing = RingShm<MarketData, MARKET_DATA_SHM_CAPACITY>;
using SignalRing = RingShm<Signal, SIGNAL_SHM_CAPACITY>;

} // namespace cts
