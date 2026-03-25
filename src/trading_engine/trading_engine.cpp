#include "trading_engine.hpp"

TradingEngine::TradingEngine(const SystemConfig& config, SignalRing* signal_ring, ExecutionReportRing* report_ring)
    : config_(config), signal_ring_(signal_ring), report_ring_(report_ring) {}

void TradingEngine::Init() {
    INFO("Start trading engine init");

    client_ = std::make_unique<OkxClient>(config_.exchange);
    client_->LoginPrivate();
    INFO("Login private ws success");

    balances_ = client_->QueryBalances();
    position_manager_.InitFromExchange(balances_);

    client_->OnOrderUpdate([this](const ExecutionReport& report) { HandleOrderUpdate(report); });

    client_->OnBalanceUpdate([this](const std::string& currency, double available, double frozen) {
        position_manager_.SyncFromExchange(currency, available, frozen);
    });

    std::thread listener_thread([this]() { RunOrderListener(); });
    listener_thread.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    client_->SubscribePrivateChannel(OkxChannelOrders, "SPOT");
    INFO("Subscribe orders channel success: [INST_TYPE] SPOT");

    client_->SubscribePrivateChannel(OkxChannelAccount, "");
    INFO("Subscribe account channel success");

    pending_orders_ = client_->QueryPendingOrders("SPOT");

    INFO("Trading engine init complete");
}

void TradingEngine::Run() {
    if (!config_.trading_engine.cpu_affinity.empty()) {
        BindThreadToCpus(config_.trading_engine.cpu_affinity);
    }

    RunOrderDispatcher();
    INFO("Stop trading engine");
}

void TradingEngine::Stop() { running_ = false; }

void TradingEngine::RunOrderDispatcher() {
    while (running_) {
        Signal signal{};
        if (!shm_pop(signal_ring_, signal)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        if (signal.action == Action::CANCEL) {
            client_->SendCancelOrder(signal.instrument, signal.order_id);
            INFO("Send cancel order: [INSTRUMENT] " + std::string(signal.instrument) + ", [ORDER_ID] " +
                 std::string(signal.order_id));
        } else {
            OrderRequest request;
            request.instrument = signal.instrument;
            request.side = (signal.action == Action::BUY) ? Side::BUY : Side::SELL;
            request.order_type = signal.order_type;
            request.size = std::to_string(signal.volume);
            if (signal.order_type == OrderType::LIMIT) {
                request.price = std::to_string(signal.price);
            }

            std::string price_str = request.price.empty() ? "MARKET" : request.price;
            INFO("Send place order: [INSTRUMENT] " + request.instrument + ", [SIDE] " +
                 std::string(ToString(request.side)) + ", [PRICE] " + price_str + ", [VOLUME] " + request.size);

            client_->SendPlaceOrder(request);
        }
    }
}

void TradingEngine::RunOrderListener() { client_->StartPrivateListener(); }

void TradingEngine::HandleOrderUpdate(const ExecutionReport& report) {
    shm_push(report_ring_, report);

    std::string base_log = "Receive execution report: [ORDER_ID] " + std::string(report.order_id) + ", [INSTRUMENT] " +
                           std::string(report.instrument) + ", [STATUS] " + ToString(report.status);

    if (report.status == OrderStatus::FILLED || report.status == OrderStatus::PARTIALLY_FILLED) {
        position_manager_.UpdateOnFill(report);
        INFO(base_log + ", [FILLED_VOLUME] " + std::to_string(report.filled_volume) + ", [AVG_PRICE] " +
             std::to_string(report.avg_fill_price));
    } else if (report.status == OrderStatus::CANCELLED) {
        position_manager_.UpdateOnCancel(report);
        INFO(base_log);
    } else if (report.status == OrderStatus::REJECTED) {
        ERROR(base_log);
    } else if (report.status == OrderStatus::NEW) {
        position_manager_.UpdateOnNew(report);
        INFO(base_log);
    }
}
