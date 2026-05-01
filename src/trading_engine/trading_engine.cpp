#include "trading_engine.hpp"

#include "common/cpu_affinity.hpp"
#include "common/logger.hpp"

#include <chrono>
#include <immintrin.h>
#include <thread>
#include <unordered_set>
#include <utils.hpp>

static std::string LockedCurrency(const char* instrument, Side side) {
    const char* dash = std::strchr(instrument, '-');
    if (!dash) {
        return instrument;
    }
    return (side == Side::SELL) ? std::string(instrument, dash) : std::string(dash + 1);
}

TradingEngine::TradingEngine(const SystemConfig& config, SignalRing* signal_ring, ExecutionReportRing* report_ring)
    : config_(config), signal_ring_(signal_ring), report_ring_(report_ring) {
    for (const auto& instrument : config_.quotation_engine.instruments) {
        strategy_instruments_.insert(instrument);
    }
}

void TradingEngine::Init() {
    INFO("Start trading engine init");

    client_ = std::make_unique<OkxClient>(config_.exchange);
    client_->LoginPrivate();
    INFO("Login private ws success");

    client_->FetchAllInstruments();

    auto balances = client_->QueryBalances();
    position_manager_.InitSpotFromExchange(balances);

    client_->OnOrderUpdate([this](const ExecutionReport& report, std::string_view client_order_id) {
        HandleOrderUpdate(report, client_order_id);
    });
    client_->OnBalanceUpdate([this](const std::string& currency, double available, double frozen, double borrowed) {
        position_manager_.SyncSpotFromExchange(currency, available, frozen, borrowed);
    });

    std::thread listener_thread([this]() { RunOrderListener(); });
    listener_thread.detach();
    std::thread reconcile_thread([this]() { RunReconciler(); });
    reconcile_thread.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    client_->SubscribePrivateChannel(OkxChannelOrders, "SPOT");
    INFO("Subscribe orders channel success: [INST_TYPE] SPOT");

    client_->SubscribePrivateChannel(OkxChannelAccount, "");
    INFO("Subscribe account channel success");

    auto spot_pending = client_->QuerySpotPendingOrders();
    INFO("Query spot pending orders complete: [COUNT] " + std::to_string(spot_pending.size()));

    client_->SubscribePrivateChannel(OkxChannelOrders, "SWAP");
    INFO("Subscribe orders channel success: [INST_TYPE] SWAP");

    auto swap_positions = client_->QuerySwapPositions();
    position_manager_.InitSwapFromExchange(swap_positions);

    auto swap_pending = client_->QuerySwapPendingOrders();
    INFO("Query swap pending orders complete: [COUNT] " + std::to_string(swap_pending.size()));

    all_pending_orders_.clear();
    all_pending_orders_.reserve(spot_pending.size() + swap_pending.size());
    for (const auto& info : spot_pending) {
        all_pending_orders_.push_back(info.report);
    }
    for (const auto& info : swap_pending) {
        all_pending_orders_.push_back(info.report);
    }

    {
        std::lock_guard<std::mutex> lock(live_orders_mutex_);
        for (const auto& order : all_pending_orders_) {
            live_order_instruments_[order.order_id] = order.instrument;
        }
    }

    pending_orders_.clear();
    for (const auto& order : all_pending_orders_) {
        if (strategy_instruments_.count(order.instrument)) {
            pending_orders_.push_back(order);
        }
    }
    INFO("Strategy-relevant pending orders: [COUNT] " + std::to_string(pending_orders_.size()));

    INFO("Trading engine init complete");
}

void TradingEngine::Run() {
    BindThreadToCpus(config_.trading_engine.cpu_affinity);

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
            request.market_type = signal.market_type;
            request.position_side = signal.position_side;
            request.trade_mode = signal.trade_mode;

            const auto& info = InstrumentRegistry::Instance().Get(signal.instrument);
            request.size = Format(signal.volume, info.volume_precision);
            if (signal.order_type == OrderType::LIMIT) {
                request.price = Format(signal.price, info.price_precision);
            }

            request.client_order_id = "cts" + std::to_string(NowNs());

            if (signal.market_type == MarketType::SPOT) {
                double amount = 0.0;
                std::string currency;
                if (request.side == Side::BUY) {
                    currency = LockedCurrency(signal.instrument, Side::BUY);
                    amount = Decode(signal.price, info.price_precision) * Decode(signal.volume, info.volume_precision);
                } else if (request.trade_mode == TradeMode::CASH) {
                    currency = LockedCurrency(signal.instrument, Side::SELL);
                    amount = Decode(signal.volume, info.volume_precision);
                }
                if (amount > 0.0) {
                    std::lock_guard<std::mutex> lock(client_order_id_mutex_);
                    client_order_id_to_reservation_[request.client_order_id] =
                        ReservationInfo{signal.instrument, currency, amount, request.side, request.trade_mode};
                }
            }

            client_->SendPlaceOrder(request);
        }
    }
}

void TradingEngine::RunOrderListener() { client_->StartPrivateListener(); }

void TradingEngine::RunReconciler() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        if (!running_)
            break;

        ReconcileOrders();
        ReconcileBalances();
        ReconcileSwapPositions();
    }
}

void TradingEngine::ReconcileOrders() {
    auto remote_spot = client_->QuerySpotPendingOrders();
    std::unordered_set<std::string> remote_active;
    std::unordered_map<std::string, ExecutionReport> remote_by_client_order_id;
    for (const auto& info : remote_spot) {
        remote_active.insert(info.report.order_id);
        if (!info.client_order_id.empty()) {
            remote_by_client_order_id[info.client_order_id] = info.report;
        }
    }

    auto remote_swap = client_->QuerySwapPendingOrders();
    for (const auto& info : remote_swap) {
        remote_active.insert(info.report.order_id);
        if (!info.client_order_id.empty()) {
            remote_by_client_order_id[info.client_order_id] = info.report;
        }
    }

    std::vector<std::pair<std::string, std::string>> orphans;
    {
        std::lock_guard<std::mutex> lock(live_orders_mutex_);
        for (const auto& [id, instrument] : live_order_instruments_) {
            if (!remote_active.contains(id)) {
                orphans.emplace_back(id, instrument);
            }
        }
    }

    if (!orphans.empty()) {
        INFO("Reconcile orders: [ORPHAN_COUNT] " + std::to_string(orphans.size()));

        for (const auto& [id, instrument] : orphans) {
            ExecutionReport report{};
            std::string client_order_id;
            if (!client_->QueryOrderById(instrument, id, report, client_order_id)) {
                WARN("Query orphan order failed: [ORDER_ID] " + id + ", [INSTRUMENT] " + instrument);
                continue;
            }
            if (report.status == OrderStatus::NEW || report.status == OrderStatus::PARTIALLY_FILLED) {
                INFO("Skip orphan still live on remote: [ORDER_ID] " + id + ", [INSTRUMENT] " + instrument);
                continue;
            }
            INFO("Replay missed terminal: [ORDER_ID] " + id + ", [INSTRUMENT] " + instrument + ", [STATUS] " +
                 ToString(report.status));
            HandleOrderUpdate(report, client_order_id);
        }
    }

    std::vector<std::string> pending_client_order_ids;
    {
        std::lock_guard<std::mutex> lock(client_order_id_mutex_);
        pending_client_order_ids.reserve(client_order_id_to_reservation_.size());
        for (const auto& [client_order_id, _] : client_order_id_to_reservation_) {
            pending_client_order_ids.push_back(client_order_id);
        }
    }
    for (const auto& client_order_id : pending_client_order_ids) {
        auto remote_it = remote_by_client_order_id.find(client_order_id);
        if (remote_it == remote_by_client_order_id.end()) {
            continue;
        }
        bool already_live = false;
        {
            std::lock_guard<std::mutex> lock(live_orders_mutex_);
            already_live =
                live_order_instruments_.find(std::string(remote_it->second.order_id)) != live_order_instruments_.end();
        }
        if (already_live) {
            continue;
        }
        ExecutionReport synth = remote_it->second;
        synth.status = OrderStatus::NEW;
        INFO("Replay missed NEW from remote: [ORDER_ID] " + std::string(synth.order_id) + ", [CLIENT_ORDER_ID] " +
             client_order_id);
        HandleOrderUpdate(synth, client_order_id);
    }
}

void TradingEngine::ReconcileBalances() {
    auto balances = client_->QueryBalances();
    for (const auto& [currency, balance] : balances) {
        position_manager_.SyncSpotFromExchange(currency, std::get<0>(balance), std::get<1>(balance),
                                               std::get<2>(balance));
    }
}

void TradingEngine::ReconcileSwapPositions() {
    auto swap_positions = client_->QuerySwapPositions();
    for (const auto& [instrument, side_map] : swap_positions) {
        for (const auto& [position_side, swap_position] : side_map) {
            position_manager_.SyncSwapFromExchange(instrument, position_side, swap_position.contracts,
                                                   swap_position.average_opening_price);
        }
    }
}

void TradingEngine::HandleOrderUpdate(const ExecutionReport& report, std::string_view client_order_id) {
    std::string order_id = std::string(report.order_id);
    std::string instrument = std::string(report.instrument);
    std::string side_str = ToString(report.side);

    const auto* info = InstrumentRegistry::Instance().Find(report.instrument);

    if (report.status != OrderStatus::NEW && !client_order_id.empty()) {
        bool needs_recovery = false;
        {
            std::lock_guard<std::mutex> lock(live_orders_mutex_);
            needs_recovery = live_order_instruments_.find(order_id) == live_order_instruments_.end();
        }
        if (needs_recovery) {
            std::lock_guard<std::mutex> lock(client_order_id_mutex_);
            needs_recovery = client_order_id_to_reservation_.contains(std::string(client_order_id));
        }
        if (needs_recovery) {
            if (report.market_type == MarketType::SPOT) {
                position_manager_.UpdateSpotOnNew(report, report.trade_mode);
            }
            {
                std::lock_guard<std::mutex> lock(live_orders_mutex_);
                live_order_instruments_[order_id] = instrument;
            }
            INFO("Recovered missed NEW: [ORDER_ID] " + order_id + ", [INSTRUMENT] " + instrument + ", [CLIENT_ORDER_ID] " +
                 std::string(client_order_id));
        }
    }

    if (report.status == OrderStatus::CANCELLED) {
        std::lock_guard<std::mutex> lock(live_orders_mutex_);
        if (live_order_instruments_.erase(order_id) == 0) {
            INFO("Skip duplicate cancelled event: [ORDER_ID] " + order_id + ", [INSTRUMENT] " + instrument);
            return;
        }
    } else if (report.status == OrderStatus::NEW) {
        std::lock_guard<std::mutex> lock(live_orders_mutex_);
        live_order_instruments_[order_id] = instrument;
    } else if (report.status == OrderStatus::FILLED || report.status == OrderStatus::REJECTED) {
        std::lock_guard<std::mutex> lock(live_orders_mutex_);
        live_order_instruments_.erase(order_id);
    }

    if (!client_order_id.empty() &&
        (report.status == OrderStatus::NEW || report.status == OrderStatus::FILLED ||
         report.status == OrderStatus::CANCELLED || report.status == OrderStatus::REJECTED)) {
        std::lock_guard<std::mutex> lock(client_order_id_mutex_);
        client_order_id_to_reservation_.erase(std::string(client_order_id));
    }

    if (report.status == OrderStatus::FILLED || report.status == OrderStatus::PARTIALLY_FILLED) {
        if (report.market_type == MarketType::SWAP) {
            position_manager_.UpdateSwapOnFill(report);
        } else {
            position_manager_.UpdateSpotOnFill(report, report.trade_mode);
        }
        if (report.status == OrderStatus::PARTIALLY_FILLED) {
            INFO("Order partially filled: [ORDER_ID] " + order_id + ", [INSTRUMENT] " + instrument + ", [SIDE] " +
                 side_str + ", [FILLED_VOLUME] " + Format(report.filled_volume, info->volume_precision) +
                 ", [TOTAL_VOLUME] " + Format(report.total_volume, info->volume_precision) + ", [AVG_PRICE] " +
                 std::to_string(report.avg_fill_price));
        } else {
            INFO("Order filled: [ORDER_ID] " + order_id + ", [INSTRUMENT] " + instrument + ", [SIDE] " + side_str +
                 ", [FILLED_VOLUME] " + Format(report.filled_volume, info->volume_precision) + ", [AVG_PRICE] " +
                 std::to_string(report.avg_fill_price));
        }
    } else if (report.status == OrderStatus::CANCELLED) {
        if (report.market_type == MarketType::SWAP) {
            position_manager_.UpdateSwapOnCancel(report);
            INFO("Order cancelled: [ORDER_ID] " + order_id + ", [INSTRUMENT] " + instrument + ", [SIDE] " + side_str);
        } else {
            position_manager_.UpdateSpotOnCancel(report, report.trade_mode);
            double remaining = Decode(report.total_volume - report.filled_volume, info->volume_precision);
            std::string currency = LockedCurrency(report.instrument, report.side);
            double available = position_manager_.GetSpotPosition(currency).available;
            INFO("Order cancelled: [ORDER_ID] " + order_id + ", [INSTRUMENT] " + instrument + ", [SIDE] " + side_str +
                 ", [REMAINING] " + std::to_string(remaining) + ", [AVAILABLE] " + std::to_string(available));
        }
    } else if (report.status == OrderStatus::REJECTED) {
        if (report.market_type == MarketType::SWAP) {
            position_manager_.UpdateSwapOnRejected(report);
        } else {
            position_manager_.UpdateSpotOnRejected(report, report.trade_mode);
        }
    } else if (report.status == OrderStatus::NEW) {
        if (report.market_type != MarketType::SWAP) {
            position_manager_.UpdateSpotOnNew(report, report.trade_mode);
            std::string currency = LockedCurrency(report.instrument, report.side);
            auto pos = position_manager_.GetSpotPosition(currency);
            INFO("Order new: [ORDER_ID] " + order_id + ", [INSTRUMENT] " + instrument + ", [SIDE] " + side_str +
                 ", [PRICE] " + Format(report.price, info->price_precision) + ", [VOLUME] " +
                 Format(report.total_volume, info->volume_precision) + ", [AVAILABLE] " +
                 std::to_string(pos.available) + ", [FROZEN] " + std::to_string(pos.frozen));
        } else {
            INFO("Order new: [ORDER_ID] " + order_id + ", [INSTRUMENT] " + instrument + ", [SIDE] " + side_str);
        }
    }

    if (strategy_instruments_.count(report.instrument)) {
        shm_push(report_ring_, report);
    }
}
