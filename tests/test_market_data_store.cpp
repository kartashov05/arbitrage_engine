#include "market/MarketDataStore.hpp"

#include <gtest/gtest.h>

namespace {

arb::OrderBook make_ready_book() {
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

    return book;
}

}  // namespace

TEST(MarketDataStoreTest, StoresAndReadsBook) {
    arb::MarketDataStore store;

    auto book = make_ready_book();

    store.set_book(
        arb::Exchange::Binance,
        "BTCUSDT",
        book
    );

    EXPECT_TRUE(
        store.has_book(
            arb::Exchange::Binance,
            "BTCUSDT"
        )
    );

    EXPECT_TRUE(
        store.has_ready_book(
            arb::Exchange::Binance,
            "BTCUSDT"
        )
    );

    const auto* stored_book = store.get_book(
        arb::Exchange::Binance,
        "BTCUSDT"
    );

    ASSERT_NE(stored_book, nullptr);

    const auto best_bid = stored_book->best_bid();
    const auto best_ask = stored_book->best_ask();

    ASSERT_TRUE(best_bid.has_value());
    ASSERT_TRUE(best_ask.has_value());

    EXPECT_DOUBLE_EQ(best_bid->price, 99.0);
    EXPECT_DOUBLE_EQ(best_ask->price, 101.0);
}

TEST(MarketDataStoreTest, SeparatesExchanges) {
    arb::MarketDataStore store;

    auto binance_book = make_ready_book();
    auto mexc_book = make_ready_book();

    store.set_book(
        arb::Exchange::Binance,
        "BTCUSDT",
        binance_book
    );

    store.set_book(
        arb::Exchange::Mexc,
        "BTCUSDT",
        mexc_book
    );

    EXPECT_TRUE(
        store.has_ready_book(
            arb::Exchange::Binance,
            "BTCUSDT"
        )
    );

    EXPECT_TRUE(
        store.has_ready_book(
            arb::Exchange::Mexc,
            "BTCUSDT"
        )
    );

    EXPECT_EQ(
        store.book_count(arb::Exchange::Binance),
        1
    );

    EXPECT_EQ(
        store.book_count(arb::Exchange::Mexc),
        1
    );
}

TEST(MarketDataStoreTest, MissingBookReturnsNullptr) {
    arb::MarketDataStore store;

    const auto* book = store.get_book(
        arb::Exchange::Binance,
        "ETHUSDT"
    );

    EXPECT_EQ(book, nullptr);

    EXPECT_FALSE(
        store.has_book(
            arb::Exchange::Binance,
            "ETHUSDT"
        )
    );

    EXPECT_FALSE(
        store.has_ready_book(
            arb::Exchange::Binance,
            "ETHUSDT"
        )
    );
}

TEST(MarketDataStoreTest, ReadySymbolsReturnsInitializedBooksOnly) {
    arb::MarketDataStore store;

    auto ready_book = make_ready_book();
    arb::OrderBook empty_book;

    store.set_book(
        arb::Exchange::Binance,
        "BTCUSDT",
        ready_book
    );

    store.set_book(
        arb::Exchange::Binance,
        "ETHUSDT",
        empty_book
    );

    const auto ready_symbols = store.ready_symbols(
        arb::Exchange::Binance
    );

    ASSERT_EQ(ready_symbols.size(), 1);
    EXPECT_EQ(ready_symbols[0], "BTCUSDT");
}

TEST(MarketDataStoreTest, ClearRemovesAllBooks) {
    arb::MarketDataStore store;

    auto book = make_ready_book();

    store.set_book(
        arb::Exchange::Binance,
        "BTCUSDT",
        book
    );

    store.set_book(
        arb::Exchange::Mexc,
        "BTCUSDT",
        book
    );

    store.clear();

    EXPECT_EQ(
        store.book_count(arb::Exchange::Binance),
        0
    );

    EXPECT_EQ(
        store.book_count(arb::Exchange::Mexc),
        0
    );
}