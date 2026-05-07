#include "exchanges/MexcDepthParser.hpp"

#include "PushDataV3ApiWrapper.pb.h"

#include <gtest/gtest.h>

TEST(MexcDepthParserTest, ParsesAggreDepthUpdate) {
    PushDataV3ApiWrapper wrapper;

    wrapper.set_channel("spot@public.aggre.depth.v3.api.pb@100ms@BTCUSDT");
    wrapper.set_symbol("BTCUSDT");
    wrapper.set_sendtime(1736411507002);

    auto* depths = wrapper.mutable_publicaggredepths();

    depths->set_eventtype("spot@public.aggre.depth.v3.api.pb@100ms");
    depths->set_fromversion("10589632359");
    depths->set_toversion("10589632360");

    auto* bid = depths->add_bids();
    bid->set_price("92877.58");
    bid->set_quantity("0.12345678");

    auto* ask = depths->add_asks();
    ask->set_price("92878.00");
    ask->set_quantity("1.25000000");

    const std::string payload = wrapper.SerializeAsString();

    const arb::MexcDepthParser parser;

    const auto update = parser.parse(payload);

    ASSERT_TRUE(update.has_value());

    EXPECT_EQ(update->exchange, arb::Exchange::Mexc);
    EXPECT_EQ(update->symbol, "BTCUSDT");

    EXPECT_EQ(update->first_update_id, 10589632359ULL);
    EXPECT_EQ(update->final_update_id, 10589632360ULL);
    EXPECT_EQ(update->exchange_event_time_ms, 1736411507002);

    ASSERT_EQ(update->bids.size(), 1);
    ASSERT_EQ(update->asks.size(), 1);

    EXPECT_DOUBLE_EQ(update->bids[0].price, 92877.58);
    EXPECT_DOUBLE_EQ(update->bids[0].quantity, 0.12345678);

    EXPECT_DOUBLE_EQ(update->asks[0].price, 92878.00);
    EXPECT_DOUBLE_EQ(update->asks[0].quantity, 1.25);
}

TEST(MexcDepthParserTest, IgnoresNonProtobufPayload) {
    const std::string payload = R"json({"method":"SUBSCRIPTION","code":0})json";

    const arb::MexcDepthParser parser;

    const auto update = parser.parse(payload);

    EXPECT_FALSE(update.has_value());
}

TEST(MexcDepthParserTest, IgnoresWrapperWithoutAggreDepths) {
    PushDataV3ApiWrapper wrapper;

    wrapper.set_channel("some-other-channel");
    wrapper.set_symbol("BTCUSDT");
    wrapper.set_sendtime(1736411507002);

    const std::string payload = wrapper.SerializeAsString();

    const arb::MexcDepthParser parser;

    const auto update = parser.parse(payload);

    EXPECT_FALSE(update.has_value());
}