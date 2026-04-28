#include <log.hpp>

namespace {
SdkLogFn g_info_fn = nullptr;
SdkLogFn g_warn_fn = nullptr;
SdkLogFn g_error_fn = nullptr;
} // namespace

extern "C" void sdk_set_logger(SdkLogFn info, SdkLogFn warn, SdkLogFn error) {
    g_info_fn = info;
    g_warn_fn = warn;
    g_error_fn = error;
}

extern "C" void sdk_log_info(const char* msg) {
    if (g_info_fn) {
        g_info_fn(msg);
    }
}

extern "C" void sdk_log_warn(const char* msg) {
    if (g_warn_fn) {
        g_warn_fn(msg);
    }
}

extern "C" void sdk_log_error(const char* msg) {
    if (g_error_fn) {
        g_error_fn(msg);
    }
}
