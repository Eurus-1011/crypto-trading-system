#pragma once

#include "ring_shm.hpp"

#include <cstdint>
#include <cstring>
#include <string>

enum class Side : int8_t { BUY = 1, SELL = -1 };
enum class OrderType : int8_t { MARKET = 0, LIMIT = 1 };
enum class Action : int8_t { BUY = 1, SELL = -1, CANCEL = 0 };

enum class OrderStatus : int8_t { NEW, FILLED, PARTIALLY_FILLED, CANCELLED, CANCEL_FAILED, REJECTED };

struct alignas(64) Signal {
    uint64_t timestamp_ns;
    char instrument[32];
    char order_id[32];
    Action action;
    OrderType order_type;
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
    double price;
    double filled_volume;
    double total_volume;
    double avg_fill_price;

    void SetInstrument(const char* src) {
        std::strncpy(instrument, src, sizeof(instrument) - 1);
        instrument[sizeof(instrument) - 1] = '\0';
    }

    void SetOrderId(const char* src) {
        std::strncpy(order_id, src, sizeof(order_id) - 1);
        order_id[sizeof(order_id) - 1] = '\0';
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
static constexpr const char* SHM_EXECUTION_REPORT = "/cts_exec_report";

using SignalRing = RingShm<Signal, 1024>;
using ExecutionReportRing = RingShm<ExecutionReport, 4096>;
