#pragma once

#include "strategy_engine/strategy.hpp"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

class StrategyRegistry {
  public:
    using FactoryFunc = std::function<std::unique_ptr<Strategy>()>;

    static StrategyRegistry& Instance() {
        static StrategyRegistry inst;
        return inst;
    }

    void Register(const std::string& name, FactoryFunc factory) {
        std::lock_guard<std::mutex> lock(mutex_);
        factories_[name] = std::move(factory);
    }

    std::unique_ptr<Strategy> Create(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = factories_.find(name);
        if (it == factories_.end()) {
            return nullptr;
        }
        return it->second();
    }

    bool Exists(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return factories_.find(name) != factories_.end();
    }

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

#define REGISTER_STRATEGY(StrategyClassName)                                                                           \
    static StrategyRegistrar<StrategyClassName> _##StrategyClassName(#StrategyClassName);
