#include "core/BookUpdate.hpp"
#include "core/OrderBook.hpp"

#include <gtest/gtest.h>

#include <vector>

TEST(OrderBookTest, SnapshotInitializesBook) {
    arb::OrderBook book;

    const std::vector<arb::PriceLevel> bids = {
        {.price = 99.0, .quantity = 1.0},
        {.price = 98.0, .quantity = 2.0}
    };

    const std::vector<arb::PriceLevel> asks = {
        {.price = 101.0, .quantity = 1.5},
        {.price = 102.0, .quantity = 2.5}
    };

    book.apply_snapshot(bids, asks, 100);

    ASSERT_TRUE(book.initialized());
    EXPECT_EQ(book.last_update_id(), 100);
    EXPECT_EQ(book.bid_levels(), 2);
    EXPECT_EQ(book.ask_levels(), 2);

    const auto best_bid = book.best_bid();
    const auto best_ask = book.best_ask();

    ASSERT_TRUE(best_bid.has_value());
    ASSERT_TRUE(best_ask.has_value());

    EXPECT_DOUBLE_EQ(best_bid->price, 99.0);
    EXPECT_DOUBLE_EQ(best_bid->quantity, 1.0);

    EXPECT_DOUBLE_EQ(best_ask->price, 101.0);
    EXPECT_DOUBLE_EQ(best_ask->quantity, 1.5);
}

TEST(OrderBookTest, AppliesUpdate) {
    arb::OrderBook book;

    book.apply_snapshot(
        {
            {.price = 99.0, .quantity = 1.0},
            {.price = 98.0, .quantity = 2.0}
        },
        {
            {.price = 101.0, .quantity = 1.5},
            {.price = 102.0, .quantity = 2.5}
        },
        100
    );

    arb::BookUpdate update{
        .exchange = arb::Exchange::Binance,
        .symbol = "BTCUSDT",
        .bids = {
            {.price = 100.0, .quantity = 3.0}
        },
        .asks = {
            {.price = 101.0, .quantity = 0.5}
        },
        .first_update_id = 101,
        .final_update_id = 101
    };

    const auto result = book.apply_update(update);

    EXPECT_EQ(result, arb::ApplyUpdateResult::Applied);
    EXPECT_EQ(book.last_update_id(), 101);

    const auto best_bid = book.best_bid();
    const auto best_ask = book.best_ask();

    ASSERT_TRUE(best_bid.has_value());
    ASSERT_TRUE(best_ask.has_value());

    EXPECT_DOUBLE_EQ(best_bid->price, 100.0);
    EXPECT_DOUBLE_EQ(best_bid->quantity, 3.0);

    EXPECT_DOUBLE_EQ(best_ask->price, 101.0);
    EXPECT_DOUBLE_EQ(best_ask->quantity, 0.5);
}

TEST(OrderBookTest, RemovesLevelWhenQuantityIsZero) {
    arb::OrderBook book;

    book.apply_snapshot(
        {
            {.price = 99.0, .quantity = 1.0},
            {.price = 98.0, .quantity = 2.0}
        },
        {
            {.price = 101.0, .quantity = 1.5}
        },
        100
    );

    arb::BookUpdate update{
        .exchange = arb::Exchange::Binance,
        .symbol = "BTCUSDT",
        .bids = {
            {.price = 99.0, .quantity = 0.0}
        },
        .asks = {},
        .first_update_id = 101,
        .final_update_id = 101
    };

    const auto result = book.apply_update(update);

    EXPECT_EQ(result, arb::ApplyUpdateResult::Applied);
    EXPECT_EQ(book.bid_levels(), 1);

    const auto best_bid = book.best_bid();

    ASSERT_TRUE(best_bid.has_value());
    EXPECT_DOUBLE_EQ(best_bid->price, 98.0);
}

TEST(OrderBookTest, IgnoresStaleUpdate) {
    arb::OrderBook book;

    book.apply_snapshot(
        {
            {.price = 99.0, .quantity = 1.0}
        },
        {
            {.price = 101.0, .quantity = 1.0}
        },
        100
    );

    arb::BookUpdate stale_update{
        .exchange = arb::Exchange::Binance,
        .symbol = "BTCUSDT",
        .bids = {
            {.price = 100.0, .quantity = 10.0}
        },
        .asks = {},
        .first_update_id = 90,
        .final_update_id = 99
    };

    const auto result = book.apply_update(stale_update);

    EXPECT_EQ(result, arb::ApplyUpdateResult::IgnoredStale);
    EXPECT_EQ(book.last_update_id(), 100);

    const auto best_bid = book.best_bid();

    ASSERT_TRUE(best_bid.has_value());
    EXPECT_DOUBLE_EQ(best_bid->price, 99.0);
}

TEST(OrderBookTest, DetectsGap) {
    arb::OrderBook book;

    book.apply_snapshot(
        {
            {.price = 99.0, .quantity = 1.0}
        },
        {
            {.price = 101.0, .quantity = 1.0}
        },
        100
    );

    arb::BookUpdate update_with_gap{
        .exchange = arb::Exchange::Binance,
        .symbol = "BTCUSDT",
        .bids = {
            {.price = 100.0, .quantity = 10.0}
        },
        .asks = {},
        .first_update_id = 105,
        .final_update_id = 106
    };

    const auto result = book.apply_update(update_with_gap);

    EXPECT_EQ(result, arb::ApplyUpdateResult::GapDetected);
    EXPECT_EQ(book.last_update_id(), 100);

    const auto best_bid = book.best_bid();

    ASSERT_TRUE(best_bid.has_value());
    EXPECT_DOUBLE_EQ(best_bid->price, 99.0);
}

TEST(OrderBookTest, CannotApplyUpdateBeforeSnapshot) {
    arb::OrderBook book;

    arb::BookUpdate update{
        .exchange = arb::Exchange::Binance,
        .symbol = "BTCUSDT",
        .bids = {
            {.price = 100.0, .quantity = 1.0}
        },
        .asks = {},
        .first_update_id = 1,
        .final_update_id = 1
    };

    const auto result = book.apply_update(update);

    EXPECT_EQ(result, arb::ApplyUpdateResult::NotInitialized);
    EXPECT_FALSE(book.initialized());
}

TEST(OrderBookTest, ReturnsTopLevels) {
    arb::OrderBook book;

    book.apply_snapshot(
        {
            {.price = 100.0, .quantity = 1.0},
            {.price = 99.0, .quantity = 2.0},
            {.price = 98.0, .quantity = 3.0}
        },
        {
            {.price = 101.0, .quantity = 1.0},
            {.price = 102.0, .quantity = 2.0},
            {.price = 103.0, .quantity = 3.0}
        },
        100
    );

    const auto top_bids = book.top_bids(2);
    const auto top_asks = book.top_asks(2);

    ASSERT_EQ(top_bids.size(), 2);
    ASSERT_EQ(top_asks.size(), 2);

    EXPECT_DOUBLE_EQ(top_bids[0].price, 100.0);
    EXPECT_DOUBLE_EQ(top_bids[1].price, 99.0);

    EXPECT_DOUBLE_EQ(top_asks[0].price, 101.0);
    EXPECT_DOUBLE_EQ(top_asks[1].price, 102.0);
}