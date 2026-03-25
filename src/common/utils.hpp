#pragma once

#include <cstdint>
#include <json/json.h>
#include <string>
#include <time.h>

inline uint64_t NowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

inline uint64_t WallNs() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

inline double ParseDouble(const Json::Value& val) {
    if (val.isDouble()) {
        return val.asDouble();
    }
    if (val.isString()) {
        const std::string& s = val.asString();
        if (s.empty()) {
            return 0.0;
        }
        try {
            return std::stod(s);
        } catch (...) {
            return 0.0;
        }
    }
    return 0.0;
}

inline uint64_t ParseUint64(const Json::Value& val) {
    if (val.isUInt64()) {
        return val.asUInt64();
    }
    if (val.isString()) {
        const std::string& s = val.asString();
        if (s.empty()) {
            return 0;
        }
        try {
            return std::stoull(s);
        } catch (...) {
            return 0;
        }
    }
    return 0;
}
