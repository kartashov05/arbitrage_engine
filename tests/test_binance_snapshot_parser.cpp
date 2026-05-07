#include "exchanges/BinanceSnapshotParser.hpp"

#include <gtest/gtest.h>

TEST(BinanceSnapshotParserTest, ParsesDepthSnapshot) {
    const std::string body = R"json(
        {
          "lastUpdateId": 1027024,
          "bids": [
            ["50000.10", "1.25"],
            ["49999.90", "2.50"]
          ],
          "asks": [
            ["50001.20", "0.75"],
            ["50002.00", "3.10"]
          ]
        }
    )json";

    const arb::BinanceSnapshotParser parser;

    const auto snapshot = parser.parse(body, "BTCUSDT");

    EXPECT_EQ(snapshot.exchange, arb::Exchange::Binance);
    EXPECT_EQ(snapshot.symbol, "BTCUSDT");
    EXPECT_EQ(snapshot.last_update_id, 1027024);

    ASSERT_EQ(snapshot.bids.size(), 2);
    ASSERT_EQ(snapshot.asks.size(), 2);

    EXPECT_DOUBLE_EQ(snapshot.bids[0].price, 50000.10);
    EXPECT_DOUBLE_EQ(snapshot.bids[0].quantity, 1.25);

    EXPECT_DOUBLE_EQ(snapshot.asks[0].price, 50001.20);
    EXPECT_DOUBLE_EQ(snapshot.asks[0].quantity, 0.75);
}

TEST(BinanceSnapshotParserTest, ThrowsWhenSnapshotIsInvalid) {
    const std::string body = R"json(
        {
          "bids": [],
          "asks": []
        }
    )json";

    const arb::BinanceSnapshotParser parser;

    EXPECT_THROW(
        {
            const auto snapshot = parser.parse(body, "BTCUSDT");
            static_cast<void>(snapshot);
        },
        std::runtime_error
    );
}