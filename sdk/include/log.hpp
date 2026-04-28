#pragma once

extern "C" void sdk_log_info(const char* msg);
extern "C" void sdk_log_warn(const char* msg);
extern "C" void sdk_log_error(const char* msg);

using SdkLogFn = void (*)(const char*);

extern "C" void sdk_set_logger(SdkLogFn info, SdkLogFn warn, SdkLogFn error);

#define SDK_INFO(msg) ::sdk_log_info(std::string(msg).c_str())
#define SDK_WARN(msg) ::sdk_log_warn(std::string(msg).c_str())
#define SDK_ERROR(msg) ::sdk_log_error(std::string(msg).c_str())
