#pragma once

#include "clients/okx/okx.hpp"
#include "common/config.hpp"
#include "common/cpu_affinity.hpp"
#include "common/logger.hpp"
#include "common/quotation.hpp"

#include <atomic>
#include <thread>

class QuotationEngine {
  public:
    QuotationEngine(const SystemConfig& config, TickerRing* ticker_ring, BBORing* bbo_ring, DepthRing* depth_ring,
                    TradeRing* trade_ring)
        : config_(config), ticker_ring_(ticker_ring), bbo_ring_(bbo_ring), depth_ring_(depth_ring),
          trade_ring_(trade_ring) {}

    void Run() {
        if (!config_.quotation_engine.cpu_affinity.empty()) {
            BindThreadToCpus(config_.quotation_engine.cpu_affinity);
        }

        INFO("Start quotation engine");

        OkxClient client(config_.exchange);

        client.OnTicker([this](const Ticker& ticker) { shm_push(ticker_ring_, ticker); });
        client.OnBBO([this](const BBO& bbo) { shm_push(bbo_ring_, bbo); });
        client.OnDepth([this](const Depth& depth) { shm_push(depth_ring_, depth); });
        client.OnTrade([this](const Trade& trade) { shm_push(trade_ring_, trade); });

        for (auto& instrument : config_.quotation_engine.instruments) {
            for (auto& channel : config_.quotation_engine.channels) {
                client.Subscribe(channel, instrument);
                INFO("Subscribe channel success: [CHANNEL] " + channel + ", [INSTRUMENT] " + instrument);
            }
        }

        client_ = &client;
        client.Start();
        INFO("Stop quotation engine");
    }

    void Stop() {
        if (client_) {
            client_->Stop();
        }
    }

  private:
    const SystemConfig& config_;
    TickerRing* ticker_ring_;
    BBORing* bbo_ring_;
    DepthRing* depth_ring_;
    TradeRing* trade_ring_;
    ExchangeClient* client_ = nullptr;
};
