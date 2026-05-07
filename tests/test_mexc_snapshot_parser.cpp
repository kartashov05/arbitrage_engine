#include "exchanges/MexcSnapshotParser.hpp"

#include <gtest/gtest.h>

TEST(MexcSnapshotParserTest, ParsesDepthSnapshot) {
    const std::string body = R"json(
        {
          "lastUpdateId": 451234567890,
          "bids": [
            ["68950.10", "2.345"],
            ["68949.80", "0.512"]
          ],
          "asks": [
            ["68950.20", "1.104"],
            ["68950.40", "3.872"]
          ]
        }
    )json";

    const arb::MexcSnapshotParser parser;

    const auto snapshot = parser.parse(body, "BTCUSDT");

    EXPECT_EQ(snapshot.exchange, arb::Exchange::Mexc);
    EXPECT_EQ(snapshot.symbol, "BTCUSDT");
    EXPECT_EQ(snapshot.last_update_id, 451234567890);

    ASSERT_EQ(snapshot.bids.size(), 2);
    ASSERT_EQ(snapshot.asks.size(), 2);

    EXPECT_DOUBLE_EQ(snapshot.bids[0].price, 68950.10);
    EXPECT_DOUBLE_EQ(snapshot.bids[0].quantity, 2.345);

    EXPECT_DOUBLE_EQ(snapshot.asks[0].price, 68950.20);
    EXPECT_DOUBLE_EQ(snapshot.asks[0].quantity, 1.104);
}

TEST(MexcSnapshotParserTest, ThrowsWhenSnapshotIsInvalid) {
    const std::string body = R"json(
        {
          "bids": [],
          "asks": []
        }
    )json";

    const arb::MexcSnapshotParser parser;

    EXPECT_THROW(
        {
            const auto snapshot = parser.parse(body, "BTCUSDT");
            static_cast<void>(snapshot);
        },
        std::runtime_error
    );
}