#include "planner/SubscriptionPlanner.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(SubscriptionPlannerTest, BuildsBinanceStreamName) {
    const auto stream = arb::SubscriptionPlanner::build_stream_name(
        arb::Exchange::Binance,
        "BTCUSDT"
    );

    EXPECT_EQ(stream, "btcusdt@depth@100ms");
}

TEST(SubscriptionPlannerTest, BuildsMexcStreamName) {
    const auto stream = arb::SubscriptionPlanner::build_stream_name(
        arb::Exchange::Mexc,
        "btcusdt"
    );

    EXPECT_EQ(stream, "spot@public.aggre.depth.v3.api.pb@100ms@BTCUSDT");
}

TEST(SubscriptionPlannerTest, GroupsSymbolsByLimit) {
    std::vector<std::string> symbols;

    for (int i = 0; i < 75; ++i) {
        symbols.push_back("SYMBOL" + std::to_string(i) + "USDT");
    }

    const arb::SubscriptionPlanner planner;

    const auto groups = planner.plan(
        arb::Exchange::Mexc,
        symbols,
        30
    );

    ASSERT_EQ(groups.size(), 3);

    EXPECT_EQ(groups[0].symbols.size(), 30);
    EXPECT_EQ(groups[1].symbols.size(), 30);
    EXPECT_EQ(groups[2].symbols.size(), 15);

    EXPECT_EQ(groups[0].connection_index, 0);
    EXPECT_EQ(groups[1].connection_index, 1);
    EXPECT_EQ(groups[2].connection_index, 2);
}

TEST(SubscriptionPlannerTest, BuildsBinanceCombinedTarget) {
    const std::vector<std::string> streams = {
        "btcusdt@depth@100ms",
        "ethusdt@depth@100ms",
        "solusdt@depth@100ms"
    };

    const auto target =
        arb::SubscriptionPlanner::build_binance_combined_target(streams);

    EXPECT_EQ(
        target,
        "/stream?streams=btcusdt@depth@100ms/ethusdt@depth@100ms/solusdt@depth@100ms"
    );
}

TEST(SubscriptionPlannerTest, ThrowsOnZeroLimit) {
    const arb::SubscriptionPlanner planner;

    const std::vector<std::string> symbols = {
        "BTCUSDT",
        "ETHUSDT"
    };

    EXPECT_THROW(
        planner.plan(arb::Exchange::Binance, symbols, 0),
        std::invalid_argument
    );
}

TEST(SubscriptionPlannerTest, EmptySymbolsProducesNoGroups) {
    const arb::SubscriptionPlanner planner;

    const std::vector<std::string> symbols;

    const auto groups = planner.plan(
        arb::Exchange::Binance,
        symbols,
        100
    );

    EXPECT_TRUE(groups.empty());
}