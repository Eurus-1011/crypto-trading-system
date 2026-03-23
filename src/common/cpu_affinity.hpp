#pragma once

#include <cerrno>
#include <cstring>
#include <sched.h>
#include <string>
#include <vector>

inline bool BindThreadToCpus(const std::vector<int>& cpu_ids) {
    if (cpu_ids.empty()) {
        return false;
    }
    cpu_set_t mask;
    CPU_ZERO(&mask);
    for (int cpu_id : cpu_ids) {
        CPU_SET(cpu_id, &mask);
    }
    return sched_setaffinity(0, sizeof(mask), &mask) == 0;
}
