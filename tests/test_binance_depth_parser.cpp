#include "exchanges/BinanceDepthParser.hpp"

#include <gtest/gtest.h>

TEST(BinanceDepthParserTest, ParsesCombinedDepthUpdate) {
    const std::string message = R"json(
        {
          "stream": "btcusdt@depth@100ms",
          "data": {
            "e": "depthUpdate",
            "E": 1672515782136,
            "s": "BTCUSDT",
            "U": 100,
            "u": 105,
            "b": [
              ["50000.10", "1.25"],
              ["49999.90", "2.50"]
            ],
            "a": [
              ["50001.20", "0.75"],
              ["50002.00", "3.10"]
            ]
          }
        }
    )json";

    const arb::BinanceDepthParser parser;

    const auto update = parser.parse(message);

    ASSERT_TRUE(update.has_value());

    EXPECT_EQ(update->exchange, arb::Exchange::Binance);
    EXPECT_EQ(update->symbol, "BTCUSDT");

    EXPECT_EQ(update->first_update_id, 100);
    EXPECT_EQ(update->final_update_id, 105);
    EXPECT_EQ(update->exchange_event_time_ms, 1672515782136);

    ASSERT_EQ(update->bids.size(), 2);
    ASSERT_EQ(update->asks.size(), 2);

    EXPECT_DOUBLE_EQ(update->bids[0].price, 50000.10);
    EXPECT_DOUBLE_EQ(update->bids[0].quantity, 1.25);

    EXPECT_DOUBLE_EQ(update->asks[0].price, 50001.20);
    EXPECT_DOUBLE_EQ(update->asks[0].quantity, 0.75);
}

TEST(BinanceDepthParserTest, IgnoresNonDepthMessages) {
    const std::string message = R"json(
        {
          "stream": "btcusdt@trade",
          "data": {
            "e": "trade",
            "E": 1672515782136,
            "s": "BTCUSDT"
          }
        }
    )json";

    const arb::BinanceDepthParser parser;

    const auto update = parser.parse(message);

    EXPECT_FALSE(update.has_value());
}