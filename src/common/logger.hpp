#pragma once

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/core/PatternFormatterOptions.h"
#include "quill/sinks/FileSink.h"

inline std::atomic<quill::Logger*> global_logger_ptr{nullptr};

inline void InitLog(const int cpu_affinity, const std::string& path) {
    quill::BackendOptions backend_options;
    backend_options.thread_name = "LoggerBackend";
    backend_options.log_level_descriptions[static_cast<uint32_t>(quill::LogLevel::Warning)] = "WARN";
    if (cpu_affinity >= 0) {
        backend_options.cpu_affinity = static_cast<uint16_t>(cpu_affinity);
    }
    quill::Backend::start(backend_options);

    quill::FileSinkConfig file_sink_config;
    file_sink_config.set_open_mode('w');
    auto file_sink = quill::Frontend::create_or_get_sink<quill::FileSink>(path, file_sink_config);

    quill::PatternFormatterOptions formatter_options;
    formatter_options.format_pattern = "%(time) [%(log_level)] %(message)";
    formatter_options.timestamp_pattern = "%Y-%m-%d %H:%M:%S";

    auto* logger = quill::Frontend::create_or_get_logger("root", std::move(file_sink), formatter_options);
    global_logger_ptr.store(logger, std::memory_order_release);
}

inline void ShutdownLog() {
    if (auto* logger = global_logger_ptr.load(std::memory_order_acquire)) {
        logger->flush_log();
        quill::Backend::stop();
    }
}

inline void INFO(const std::string& msg) { LOG_INFO(global_logger_ptr.load(std::memory_order_relaxed), "{}", msg); }

inline void WARN(const std::string& msg) { LOG_WARNING(global_logger_ptr.load(std::memory_order_relaxed), "{}", msg); }

inline void ERROR(const std::string& msg) { LOG_ERROR(global_logger_ptr.load(std::memory_order_relaxed), "{}", msg); }
