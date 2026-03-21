#pragma once

#include <cassert>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

namespace cts {

enum LogLevel { kInfo = 0, kWarning = 1, kError = 2 };

static constexpr const char* kLevelStr[] = {"INFO", "WARN", "ERROR"};

class Logger {
public:
    explicit Logger(const std::string& path) : fp_(std::fopen(path.c_str(), "a")) { assert(fp_); }

    ~Logger() {
        if (fp_) std::fclose(fp_);
    }

    void log(LogLevel level, const char* msg) {
        std::time_t now = std::time(nullptr);
        std::tm t{};
        char time_buf[32];
        localtime_r(&now, &t);
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &t);
        std::lock_guard<std::mutex> lk(mutex_);
        std::fprintf(fp_, "%s [%s] %s\n", time_buf, kLevelStr[level], msg);
        std::fflush(fp_);
    }

    void log(LogLevel level, const std::string& msg) { log(level, msg.c_str()); }

private:
    std::FILE* fp_;
    std::mutex mutex_;
};

inline Logger*& GetLogPtr() {
    static Logger* p = nullptr;
    return p;
}

inline void InitLog(const std::string& path) {
    Logger*& p = GetLogPtr();
    if (p) delete p;
    p = new Logger(path);
}

inline void LOG_INFO(const std::string& msg) { GetLogPtr()->log(kInfo, msg); }
inline void LOG_WARN(const std::string& msg) { GetLogPtr()->log(kWarning, msg); }
inline void LOG_ERROR(const std::string& msg) { GetLogPtr()->log(kError, msg); }

} // namespace cts
