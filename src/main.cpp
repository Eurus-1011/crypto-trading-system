#include "common/config.hpp"
#include "common/logger.hpp"
#include "quotation_engine/quotation_engine.hpp"
#include "strategy_engine/plugin_loader.hpp"
#include "strategy_engine/strategy_engine.hpp"
#include "trading_engine/trading_engine.hpp"

#include <csignal>
#include <defs.hpp>
#include <log.hpp>
#include <strategy_registry.hpp>
#include <thread>

static QuotationEngine* global_quotation_engine = nullptr;
static StrategyEngine* global_strategy_engine = nullptr;
static TradingEngine* global_trading_engine = nullptr;

static void OnSignal(int) {
    if (global_quotation_engine) {
        global_quotation_engine->Stop();
    }
    if (global_strategy_engine) {
        global_strategy_engine->Stop();
    }
    if (global_trading_engine) {
        global_trading_engine->Stop();
    }
}

static std::string JoinStrings(const std::vector<std::string>& vec) {
    std::string result;
    for (size_t idx = 0; idx < vec.size(); ++idx) {
        if (idx > 0) {
            result += ", ";
        }
        result += vec[idx];
    }
    return result;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: crypto-trading-system <config_path>\n");
        return 1;
    }

    SystemConfig config;
    std::string err;
    if (!LoadConfig(argv[1], config, err)) {
        std::fprintf(stderr, "Load config failed: %s\n", err.c_str());
        return 1;
    }

    InitLog(config.logger.cpu_affinity, config.logger.path);

    sdk_set_logger([](const char* m) { INFO(std::string(m)); }, [](const char* m) { WARN(std::string(m)); },
                   [](const char* m) { ERROR(std::string(m)); });

    INFO("System init success: [EXCHANGE] " + config.exchange.name + ", [INSTRUMENTS] " +
         JoinStrings(config.quotation_engine.instruments) + ", [CHANNELS] " +
         JoinStrings(config.quotation_engine.channels) + ", [STRATEGY] " + config.strategy_engine.name);

    for (const auto& plugin_path : config.strategy_engine.plugin_paths) {
        std::string plugin_err;
        if (!LoadPlugin(plugin_path, plugin_err)) {
            ERROR("Plugin load failed: " + plugin_err);
            return 1;
        }
        INFO("Plugin loaded: " + plugin_path);
    }

    auto* ticker_ring = shm_create<Ticker, 4096>(SHM_TICKER);
    auto* bbo_ring = shm_create<BBO, 8192>(SHM_BBO);
    auto* depth_ring = shm_create<Depth, 2048>(SHM_DEPTH);
    auto* trade_ring = shm_create<Trade, 8192>(SHM_TRADE);
    auto* signal_ring = shm_create<Signal, 1024>(SHM_SIGNAL);
    auto* report_ring = shm_create<ExecutionReport, 4096>(SHM_EXECUTION_REPORT);

    if (!ticker_ring || !bbo_ring || !depth_ring || !trade_ring || !signal_ring || !report_ring) {
        ERROR("Create SHM failed");
        return 1;
    }
    INFO("Create all SHM success");

    QuotationEngine quotation_engine(config, ticker_ring, bbo_ring, depth_ring, trade_ring);
    StrategyEngine strategy_engine(config, ticker_ring, bbo_ring, depth_ring, trade_ring, signal_ring, report_ring);
    TradingEngine trading_engine(config, signal_ring, report_ring);

    global_quotation_engine = &quotation_engine;
    global_strategy_engine = &strategy_engine;
    global_trading_engine = &trading_engine;

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    trading_engine.Init();
    strategy_engine.SetPendingOrders(trading_engine.GetPendingOrders());

    PositionManager* position_manager = &trading_engine.GetPositionManager();
    strategy_engine.SetPositionManager(position_manager);
    strategy_engine.SetFactory([&]() -> std::unique_ptr<Strategy> {
        return StrategyRegistry::Instance().Create(config.strategy_engine.name);
    });

    std::thread quotation_thread([&]() { quotation_engine.Run(); });
    std::thread strategy_thread([&]() { strategy_engine.Run(); });
    std::thread trading_thread([&]() { trading_engine.Run(); });

    INFO("Start all engines success");

    quotation_thread.join();
    strategy_thread.join();
    trading_thread.join();

    INFO("Stop all engines success, cleaning up");

    shm_destroy(SHM_TICKER, ticker_ring);
    shm_destroy(SHM_BBO, bbo_ring);
    shm_destroy(SHM_DEPTH, depth_ring);
    shm_destroy(SHM_TRADE, trade_ring);
    shm_destroy(SHM_SIGNAL, signal_ring);
    shm_destroy(SHM_EXECUTION_REPORT, report_ring);

    INFO("System shutdown complete");
    ShutdownLog();
    return 0;
}