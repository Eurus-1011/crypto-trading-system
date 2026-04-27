#include <strategy_registry.hpp>

StrategyRegistry& StrategyRegistry::Instance() {
    static StrategyRegistry inst;
    return inst;
}

void StrategyRegistry::Register(const std::string& name, FactoryFunc factory) {
    std::lock_guard<std::mutex> lock(mutex_);
    factories_[name] = std::move(factory);
}

std::unique_ptr<Strategy> StrategyRegistry::Create(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = factories_.find(name);
    if (it == factories_.end()) {
        return nullptr;
    }
    return it->second();
}

bool StrategyRegistry::Exists(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return factories_.find(name) != factories_.end();
}
