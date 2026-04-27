#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <time.h>

inline uint64_t NowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

static constexpr int64_t kPow10[] = {
    1LL,          10LL,          100LL,           1'000LL,          10'000LL,          100'000LL,           1'000'000LL,
    10'000'000LL, 100'000'000LL, 1'000'000'000LL, 10'000'000'000LL, 100'000'000'000LL, 1'000'000'000'000LL,
};

inline int64_t Encode(double value, int precision) {
    return static_cast<int64_t>(std::round(value * kPow10[precision]));
}

inline double Decode(int64_t scaled, int precision) { return static_cast<double>(scaled) / kPow10[precision]; }

inline std::string Format(int64_t scaled, int precision) {
    if (precision == 0) {
        return std::to_string(scaled);
    }
    bool negative = scaled < 0;
    int64_t abs_scaled = negative ? -scaled : scaled;
    int64_t integer_part = abs_scaled / kPow10[precision];
    int64_t frac_part = abs_scaled % kPow10[precision];
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s%lld.%0*lld", negative ? "-" : "", static_cast<long long>(integer_part),
                  precision, static_cast<long long>(frac_part));
    return buf;
}

inline int CountDecimalPlaces(const char* str) {
    const char* dot = std::strchr(str, '.');
    if (!dot) {
        return 0;
    }
    int count = 0;
    for (const char* p = dot + 1; *p; ++p) {
        ++count;
    }
    return count;
}
