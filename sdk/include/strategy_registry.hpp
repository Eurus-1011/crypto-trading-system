#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <strategy.hpp>
#include <string>

#define SDK_ABI_VERSION 1

#define DECLARE_PLUGIN_ABI()                                                                                           \
    extern "C" int sdk_plugin_abi_version() { return SDK_ABI_VERSION; }

class StrategyRegistry {
  public:
    using FactoryFunc = std::function<std::unique_ptr<Strategy>()>;

    static StrategyRegistry& Instance();

    void Register(const std::string& name, FactoryFunc factory);
    std::unique_ptr<Strategy> Create(const std::string& name);
    bool Exists(const std::string& name) const;

  private:
    StrategyRegistry() = default;

    mutable std::mutex mutex_;
    std::map<std::string, FactoryFunc> factories_;
};

template <typename T>
struct StrategyRegistrar {
    StrategyRegistrar(const char* name) {
        StrategyRegistry::Instance().Register(name, []() { return std::make_unique<T>(); });
    }
};

#define REGISTER_STRATEGY(StrategyName) static StrategyRegistrar<StrategyName> _##StrategyName(#StrategyName);
