#include "clients/okx/okx.hpp"
#include "common/quotation.hpp"
#include "common/trading.hpp"
#include "common/utils.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <thread>

static ExchangeConfig MakeLiveConfig() {
    ExchangeConfig config;
    config.name = "okx";
    config.api_key = "not_needed_for_public";
    config.secret_key = "not_needed_for_public";
    config.passphrase = "not_needed_for_public";
    return config;
}

class OkxIntegrationTest : public ::testing::Test {
  protected:
    OkxClient client{MakeLiveConfig()};
};

TEST_F(OkxIntegrationTest, ReceiveLiveTicker) {
    std::atomic<bool> received{false};
    Ticker captured{};

    client.OnTicker([&](const Ticker& ticker) {
        if (!received.load()) {
            captured = ticker;
            received.store(true);
        }
    });

    client.Subscribe("tickers", "BTC-USDT");

    std::thread ws_thread([&]() { client.Start(); });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!received.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    client.Stop();
    if (ws_thread.joinable()) {
        ws_thread.join();
    }

    ASSERT_TRUE(received.load()) << "no ticker received within 10 seconds";
    EXPECT_STREQ(captured.instrument, "BTC-USDT");
    EXPECT_GT(captured.last_price, 0.0);
    EXPECT_GT(captured.bid_price, 0.0);
    EXPECT_GT(captured.ask_price, 0.0);
    EXPECT_GE(captured.ask_price, captured.bid_price);
    EXPECT_GT(captured.exchange_ts_ms, 0ULL);
    EXPECT_GT(captured.local_ts_ns, 0ULL);
    EXPECT_GT(captured.volume_24h, 0.0);

    std::cout << "[LiveTicker] BTC-USDT"
              << " last=" << captured.last_price << " bid=" << captured.bid_price << " ask=" << captured.ask_price
              << " vol24h=" << captured.volume_24h << std::endl;
}

TEST_F(OkxIntegrationTest, ReceiveLiveBBO) {
    std::atomic<bool> received{false};
    BBO captured{};

    client.OnBBO([&](const BBO& bbo) {
        if (!received.load()) {
            captured = bbo;
            received.store(true);
        }
    });

    client.Subscribe("bbo-tbt", "BTC-USDT");

    std::thread ws_thread([&]() { client.Start(); });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!received.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    client.Stop();
    if (ws_thread.joinable()) {
        ws_thread.join();
    }

    ASSERT_TRUE(received.load()) << "no BBO received within 10 seconds";
    EXPECT_STREQ(captured.instrument, "BTC-USDT");
    EXPECT_GT(captured.bid_price, 0.0);
    EXPECT_GT(captured.ask_price, 0.0);
    EXPECT_GE(captured.ask_price, captured.bid_price);
    EXPECT_GT(captured.bid_volume, 0.0);
    EXPECT_GT(captured.ask_volume, 0.0);

    std::cout << "[LiveBBO] BTC-USDT"
              << " bid=" << captured.bid_price << "x" << captured.bid_volume << " ask=" << captured.ask_price << "x"
              << captured.ask_volume << std::endl;
}

TEST_F(OkxIntegrationTest, ReceiveLiveDepth) {
    std::atomic<bool> received{false};
    Depth captured{};

    client.OnDepth([&](const Depth& depth) {
        if (!received.load()) {
            captured = depth;
            received.store(true);
        }
    });

    client.Subscribe("books5", "BTC-USDT");

    std::thread ws_thread([&]() { client.Start(); });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!received.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    client.Stop();
    if (ws_thread.joinable()) {
        ws_thread.join();
    }

    ASSERT_TRUE(received.load()) << "no depth received within 10 seconds";
    EXPECT_STREQ(captured.instrument, "BTC-USDT");
    EXPECT_EQ(captured.ask_levels, MAX_DEPTH_LEVELS);
    EXPECT_EQ(captured.bid_levels, MAX_DEPTH_LEVELS);
    EXPECT_GT(captured.asks[0].price, 0.0);
    EXPECT_GT(captured.bids[0].price, 0.0);
    EXPECT_GE(captured.asks[0].price, captured.bids[0].price);

    for (int idx = 1; idx < captured.ask_levels; ++idx) {
        EXPECT_GE(captured.asks[idx].price, captured.asks[idx - 1].price);
    }
    for (int idx = 1; idx < captured.bid_levels; ++idx) {
        EXPECT_LE(captured.bids[idx].price, captured.bids[idx - 1].price);
    }

    std::cout << "[LiveDepth] BTC-USDT"
              << " best_bid=" << captured.bids[0].price << " best_ask=" << captured.asks[0].price
              << " spread=" << (captured.asks[0].price - captured.bids[0].price) << " levels=" << captured.ask_levels
              << std::endl;
}

TEST_F(OkxIntegrationTest, ReceiveLiveTrade) {
    std::atomic<bool> received{false};
    Trade captured{};

    client.OnTrade([&](const Trade& trade) {
        if (!received.load()) {
            captured = trade;
            received.store(true);
        }
    });

    client.Subscribe("trades", "BTC-USDT");

    std::thread ws_thread([&]() { client.Start(); });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!received.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    client.Stop();
    if (ws_thread.joinable()) {
        ws_thread.join();
    }

    ASSERT_TRUE(received.load()) << "no trade received within 15 seconds";
    EXPECT_STREQ(captured.instrument, "BTC-USDT");
    EXPECT_GT(captured.price, 0.0);
    EXPECT_GT(captured.volume, 0.0);
    EXPECT_TRUE(captured.side == TradeSide::BUY || captured.side == TradeSide::SELL);
    EXPECT_GT(std::strlen(captured.trade_id), 0u);

    std::string side_str = (captured.side == TradeSide::BUY) ? "buy" : "sell";
    std::cout << "[LiveTrade] BTC-USDT"
              << " id=" << captured.trade_id << " price=" << captured.price << " volume=" << captured.volume
              << " side=" << side_str << std::endl;
}
