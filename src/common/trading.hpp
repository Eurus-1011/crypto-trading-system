#pragma once

#include "ring_shm.hpp"

#include <cstdint>
#include <cstring>
#include <string>

enum class Side : int8_t { BUY = 1, SELL = -1 };
enum class OrderType : int8_t { MARKET = 0, LIMIT = 1 };

struct alignas(64) Signal {
    uint64_t timestamp_ns;
    char instrument[32];
    Side side;
    OrderType order_type;
    double price;
    double volume;

    void SetInstrument(const char* src) {
        std::strncpy(instrument, src, sizeof(instrument) - 1);
        instrument[sizeof(instrument) - 1] = '\0';
    }
};

struct OrderRequest {
    std::string instrument;
    Side side;
    OrderType order_type;
    std::string size;
    std::string price;
    std::string target_currency;
};

struct OrderResult {
    bool success;
    std::string order_id;
    std::string code;
    std::string message;
};

static constexpr const char* SHM_SIGNAL = "/cts_signal";

using SignalRing = RingShm<Signal, 1024>;
