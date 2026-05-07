#include "engine/ArbitrageEngine.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

arb::OrderBook make_book(
    std::vector<arb::PriceLevel> bids,
    std::vector<arb::PriceLevel> asks,
    std::uint64_t update_id = 100
) {
    arb::OrderBook book;
    book.apply_snapshot(bids, asks, update_id);
    return book;
}

arb::ArbitrageEngine make_engine(
    double min_net_spread_bps = 1.0,
    double min_notional_usdt = 10.0,
    double max_notional_usdt = 1000.0,
    double binance_fee_bps = 10.0,
    double mexc_fee_bps = 10.0
) {
    return arb::ArbitrageEngine(
        arb::ArbitrageEngineSettings{
            .min_net_spread_bps = min_net_spread_bps,
            .min_notional_usdt = min_notional_usdt,
            .max_notional_usdt = max_notional_usdt,
            .binance_taker_fee_bps = binance_fee_bps,
            .mexc_taker_fee_bps = mexc_fee_bps,
            .max_depth_levels = 50
        }
    );
}

}  // namespace

TEST(ArbitrageEngineTest, FindsBinanceBuyMexcSellOpportunity) {
    const auto binance = make_book(
        {
            {.price = 99.0, .quantity = 10.0}
        },
        {
            {.price = 100.0, .quantity = 10.0}
        }
    );

    const auto mexc = make_book(
        {
            {.price = 101.0, .quantity = 10.0}
        },
        {
            {.price = 102.0, .quantity = 10.0}
        }
    );

    const auto engine = make_engine();

    const auto opportunities = engine.scan_symbol("BTCUSDT", binance, mexc);

    ASSERT_EQ(opportunities.size(), 1);

    const auto& opportunity = opportunities[0];

    EXPECT_EQ(opportunity.symbol, "BTCUSDT");
    EXPECT_EQ(opportunity.buy_exchange, arb::Exchange::Binance);
    EXPECT_EQ(opportunity.sell_exchange, arb::Exchange::Mexc);

    EXPECT_DOUBLE_EQ(opportunity.base_quantity, 10.0);
    EXPECT_DOUBLE_EQ(opportunity.buy_vwap, 100.0);
    EXPECT_DOUBLE_EQ(opportunity.sell_vwap, 101.0);

    EXPECT_GT(opportunity.gross_profit_usdt, 0.0);
    EXPECT_GT(opportunity.net_profit_usdt, 0.0);
    EXPECT_GT(opportunity.net_spread_bps, 1.0);
}

TEST(ArbitrageEngineTest, FindsMexcBuyBinanceSellOpportunity) {
    const auto binance = make_book(
        {
            {.price = 105.0, .quantity = 5.0}
        },
        {
            {.price = 106.0, .quantity = 5.0}
        }
    );

    const auto mexc = make_book(
        {
            {.price = 99.0, .quantity = 5.0}
        },
        {
            {.price = 100.0, .quantity = 5.0}
        }
    );

    const auto engine = make_engine();

    const auto opportunities = engine.scan_symbol("ETHUSDT", binance, mexc);

    ASSERT_EQ(opportunities.size(), 1);

    const auto& opportunity = opportunities[0];

    EXPECT_EQ(opportunity.symbol, "ETHUSDT");
    EXPECT_EQ(opportunity.buy_exchange, arb::Exchange::Mexc);
    EXPECT_EQ(opportunity.sell_exchange, arb::Exchange::Binance);

    EXPECT_DOUBLE_EQ(opportunity.base_quantity, 5.0);
    EXPECT_DOUBLE_EQ(opportunity.buy_vwap, 100.0);
    EXPECT_DOUBLE_EQ(opportunity.sell_vwap, 105.0);

    EXPECT_GT(opportunity.net_profit_usdt, 0.0);
}

TEST(ArbitrageEngineTest, RejectsOpportunityWhenFeesRemoveProfit) {
    const auto binance = make_book(
        {
            {.price = 99.0, .quantity = 10.0}
        },
        {
            {.price = 100.0, .quantity = 10.0}
        }
    );

    const auto mexc = make_book(
        {
            {.price = 100.05, .quantity = 10.0}
        },
        {
            {.price = 101.0, .quantity = 10.0}
        }
    );

    const auto engine = make_engine(
        1.0,
        10.0,
        1000.0,
        10.0,
        10.0
    );

    const auto opportunities = engine.scan_symbol("SOLUSDT", binance, mexc);

    EXPECT_TRUE(opportunities.empty());
}

TEST(ArbitrageEngineTest, UsesDepthBeyondBestLevel) {
    const auto binance = make_book(
        {
            {.price = 99.0, .quantity = 100.0}
        },
        {
            {.price = 100.0, .quantity = 1.0},
            {.price = 100.5, .quantity = 9.0}
        }
    );

    const auto mexc = make_book(
        {
            {.price = 102.0, .quantity = 1.0},
            {.price = 101.0, .quantity = 9.0}
        },
        {
            {.price = 103.0, .quantity = 100.0}
        }
    );

    const auto engine = make_engine(
        1.0,
        10.0,
        2000.0,
        0.0,
        0.0
    );

    const auto opportunities = engine.scan_symbol("SOLUSDT", binance, mexc);

    ASSERT_EQ(opportunities.size(), 1);

    const auto& opportunity = opportunities[0];

    EXPECT_DOUBLE_EQ(opportunity.base_quantity, 10.0);

    // Покупка:
    // 1 * 100.0 + 9 * 100.5 = 1004.5
    EXPECT_DOUBLE_EQ(opportunity.buy_notional_usdt, 1004.5);

    // Продажа:
    // 1 * 102.0 + 9 * 101.0 = 1011.0
    EXPECT_DOUBLE_EQ(opportunity.sell_notional_usdt, 1011.0);

    EXPECT_GT(opportunity.net_profit_usdt, 0.0);
}

TEST(ArbitrageEngineTest, RespectsMaxNotionalLimit) {
    const auto binance = make_book(
        {
            {.price = 99.0, .quantity = 100.0}
        },
        {
            {.price = 100.0, .quantity = 100.0}
        }
    );

    const auto mexc = make_book(
        {
            {.price = 110.0, .quantity = 100.0}
        },
        {
            {.price = 111.0, .quantity = 100.0}
        }
    );

    const auto engine = make_engine(
        1.0,
        10.0,
        250.0,
        0.0,
        0.0
    );

    const auto opportunities = engine.scan_symbol("BTCUSDT", binance, mexc);

    ASSERT_EQ(opportunities.size(), 1);

    const auto& opportunity = opportunities[0];

    EXPECT_DOUBLE_EQ(opportunity.base_quantity, 2.5);
    EXPECT_DOUBLE_EQ(opportunity.buy_notional_usdt, 250.0);
    EXPECT_DOUBLE_EQ(opportunity.sell_notional_usdt, 275.0);
}

TEST(ArbitrageEngineTest, RejectsBelowMinNotional) {
    const auto binance = make_book(
        {
            {.price = 99.0, .quantity = 1.0}
        },
        {
            {.price = 100.0, .quantity = 0.05}
        }
    );

    const auto mexc = make_book(
        {
            {.price = 110.0, .quantity = 0.05}
        },
        {
            {.price = 111.0, .quantity = 1.0}
        }
    );

    const auto engine = make_engine(
        1.0,
        100.0,
        1000.0,
        0.0,
        0.0
    );

    const auto opportunities = engine.scan_symbol("BTCUSDT", binance, mexc);

    EXPECT_TRUE(opportunities.empty());
}

TEST(ArbitrageEngineTest, ReturnsNoOpportunityWhenBooksNotInitialized) {
    const arb::OrderBook binance;
    const arb::OrderBook mexc;

    const auto engine = make_engine();

    const auto opportunities = engine.scan_symbol("BTCUSDT", binance, mexc);

    EXPECT_TRUE(opportunities.empty());
}