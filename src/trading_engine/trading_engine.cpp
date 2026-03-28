#include "trading_engine.hpp"

TradingEngine::TradingEngine(const SystemConfig& config, SignalRing* signal_ring, ExecutionReportRing* report_ring)
    : config_(config), signal_ring_(signal_ring), report_ring_(report_ring) {}

void TradingEngine::Init() {
    INFO("Start trading engine init");

    client_ = std::make_unique<OkxClient>(config_.exchange);
    client_->LoginPrivate();
    INFO("Login private ws success");

    client_->FetchInstrumentCodes(config_.quotation_engine.instruments);

    auto balances = client_->QueryBalances();
    position_manager_.InitFromExchange(balances);

    client_->OnOrderUpdate([this](const ExecutionReport& report) { HandleOrderUpdate(report); });

    std::thread listener_thread([this]() { RunOrderListener(); });
    listener_thread.detach();
    std::thread reconcile_thread([this]() { RunReconciler(); });
    reconcile_thread.detach();
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
            _mm_pause();
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

void TradingEngine::RunReconciler() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::minutes(5));
        if (!running_)
            break;
        auto balances = client_->QueryBalances();
        for (const auto& [currency, bal] : balances) {
            position_manager_.SyncFromExchange(currency, bal.first, bal.second);
        }
    }
}

void TradingEngine::HandleOrderUpdate(const ExecutionReport& report) {
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
        position_manager_.UpdateOnRejected(report);
        ERROR(base_log);
    } else if (report.status == OrderStatus::NEW) {
        position_manager_.UpdateOnNew(report);
        INFO(base_log);
    }

    shm_push(report_ring_, report);
}
