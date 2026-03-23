#include "common/config.hpp"
#include "common/logger.hpp"
#include "common/quotation.hpp"
#include "common/trading.hpp"
#include "quotation_engine/quotation_engine.hpp"
#include "strategy_engine/strategy_engine.hpp"
#include "trading_engine/trading_engine.hpp"

#include <csignal>
#include <thread>

static const std::string LOG_PATH = "logs/system.log";

static QuotationEngine* g_quotation_engine = nullptr;
static StrategyEngine* g_strategy_engine = nullptr;
static TradingEngine* g_trading_engine = nullptr;

static void OnSignal(int) {
    if (g_quotation_engine) {
        g_quotation_engine->Stop();
    }
    if (g_strategy_engine) {
        g_strategy_engine->Stop();
    }
    if (g_trading_engine) {
        g_trading_engine->Stop();
    }
}

static std::string GetPublicIP() {
    std::array<char, 128> buffer;
    std::string result;
    FILE* pipe = popen("curl -s --connect-timeout 5 https://ifconfig.me 2>/dev/null", "r");
    if (!pipe) {
        return "unknown";
    }
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);
    if (result.empty()) {
        return "unknown";
    }
    return result;
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
    InitLog(LOG_PATH);

    if (argc < 2) {
        ERROR("Usage: crypto-trading-system <config_path>");
        return 1;
    }
    std::string config_path = argv[1];

    SystemConfig config;
    std::string err;
    if (!LoadConfig(config_path, config, err)) {
        ERROR("Load config failed: [DETAIL] " + err);
        return 1;
    }

    std::string public_ip = GetPublicIP();

    INFO("System init success: [EXCHANGE] " + config.exchange.name + ", [PUBLIC_IP] " + public_ip + ", [INSTRUMENTS] " +
         JoinStrings(config.quotation_engine.instruments) + ", [CHANNELS] " +
         JoinStrings(config.quotation_engine.channels) + ", [STRATEGY] " + config.strategy_engine.name);

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

    g_quotation_engine = &quotation_engine;
    g_strategy_engine = &strategy_engine;
    g_trading_engine = &trading_engine;

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    trading_engine.Init();
    strategy_engine.SetPendingOrders(trading_engine.GetPendingOrders());

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
    return 0;
}
