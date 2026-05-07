#include "core/BookSnapshot.hpp"
#include "core/BookUpdate.hpp"
#include "exchanges/MexcLocalBookManager.hpp"

#include <gtest/gtest.h>

namespace {

arb::BookSnapshot make_snapshot(
    std::uint64_t last_update_id = 100
) {
    return arb::BookSnapshot{
        .exchange = arb::Exchange::Mexc,
        .symbol = "BTCUSDT",
        .bids = {
            {.price = 99.0, .quantity = 1.0}
        },
        .asks = {
            {.price = 101.0, .quantity = 1.0}
        },
        .last_update_id = last_update_id,
        .local_receive_time_ms = 0
    };
}

arb::BookUpdate make_update(
    std::uint64_t first_update_id,
    std::uint64_t final_update_id,
    double bid_price,
    double bid_qty,
    double ask_price,
    double ask_qty
) {
    return arb::BookUpdate{
        .exchange = arb::Exchange::Mexc,
        .symbol = "BTCUSDT",
        .bids = {
            {.price = bid_price, .quantity = bid_qty}
        },
        .asks = {
            {.price = ask_price, .quantity = ask_qty}
        },
        .first_update_id = first_update_id,
        .final_update_id = final_update_id,
        .exchange_event_time_ms = 0,
        .local_receive_time_ms = 0
    };
}

}  // namespace

TEST(MexcLocalBookManagerTest, BuffersUpdatesBeforeSnapshot) {
    arb::MexcLocalBookManager manager{"BTCUSDT"};

    const auto update = make_update(
        101,
        101,
        100.0,
        2.0,
        102.0,
        2.0
    );

    const auto result = manager.on_update(update);

    EXPECT_EQ(result, arb::MexcLocalBookUpdateResult::Buffered);
    EXPECT_EQ(manager.buffered_updates(), 1);
    EXPECT_EQ(manager.status(), arb::MexcLocalBookStatus::WaitingForSnapshot);
    EXPECT_FALSE(manager.ready());
}

TEST(MexcLocalBookManagerTest, AppliesSnapshotAndBufferedUpdates) {
    arb::MexcLocalBookManager manager{"BTCUSDT"};

    manager.on_update(
        make_update(
            90,
            100,
            98.0,
            1.0,
            103.0,
            1.0
        )
    );

    manager.on_update(
        make_update(
            101,
            101,
            100.0,
            2.0,
            102.0,
            2.0
        )
    );

    manager.on_update(
        make_update(
            102,
            104,
            100.5,
            3.0,
            101.5,
            3.0
        )
    );

    const auto result = manager.apply_snapshot(make_snapshot(100));

    EXPECT_EQ(result, arb::MexcLocalBookUpdateResult::SnapshotApplied);
    EXPECT_TRUE(manager.ready());
    EXPECT_EQ(manager.buffered_updates(), 0);

    const auto& book = manager.book();

    EXPECT_EQ(book.last_update_id(), 104);

    const auto best_bid = book.best_bid();
    const auto best_ask = book.best_ask();

    ASSERT_TRUE(best_bid.has_value());
    ASSERT_TRUE(best_ask.has_value());

    EXPECT_DOUBLE_EQ(best_bid->price, 100.5);
    EXPECT_DOUBLE_EQ(best_bid->quantity, 3.0);

    EXPECT_DOUBLE_EQ(best_ask->price, 101.0);
    EXPECT_DOUBLE_EQ(best_ask->quantity, 1.0);
}

TEST(MexcLocalBookManagerTest, RejectsSnapshotWhenBufferedUpdatesDoNotBridgeSnapshot) {
    arb::MexcLocalBookManager manager{"BTCUSDT"};

    manager.on_update(
        make_update(
            105,
            106,
            100.0,
            2.0,
            102.0,
            2.0
        )
    );

    const auto result = manager.apply_snapshot(make_snapshot(100));

    EXPECT_EQ(result, arb::MexcLocalBookUpdateResult::SnapshotRejected);
    EXPECT_TRUE(manager.need_resync());
    EXPECT_EQ(manager.buffered_updates(), 0);
}

TEST(MexcLocalBookManagerTest, DetectsGapAfterBookIsReady) {
    arb::MexcLocalBookManager manager{"BTCUSDT"};

    const auto snapshot_result = manager.apply_snapshot(make_snapshot(100));

    EXPECT_EQ(snapshot_result, arb::MexcLocalBookUpdateResult::SnapshotApplied);
    EXPECT_TRUE(manager.ready());

    const auto update_result = manager.on_update(
        make_update(
            105,
            106,
            100.0,
            2.0,
            102.0,
            2.0
        )
    );

    EXPECT_EQ(update_result, arb::MexcLocalBookUpdateResult::GapDetected);
    EXPECT_TRUE(manager.need_resync());
}

TEST(MexcLocalBookManagerTest, IgnoresUpdatesForAnotherSymbol) {
    arb::MexcLocalBookManager manager{"BTCUSDT"};

    arb::BookUpdate update = make_update(
        101,
        101,
        100.0,
        2.0,
        102.0,
        2.0
    );

    update.symbol = "ETHUSDT";

    const auto result = manager.on_update(update);

    EXPECT_EQ(result, arb::MexcLocalBookUpdateResult::NotForSymbol);
    EXPECT_EQ(manager.buffered_updates(), 0);
}

TEST(MexcLocalBookManagerTest, ResetReturnsManagerToWaitingState) {
    arb::MexcLocalBookManager manager{"BTCUSDT"};

    manager.apply_snapshot(make_snapshot(100));

    ASSERT_TRUE(manager.ready());

    manager.reset();

    EXPECT_EQ(manager.status(), arb::MexcLocalBookStatus::WaitingForSnapshot);
    EXPECT_FALSE(manager.ready());
    EXPECT_EQ(manager.buffered_updates(), 0);
    EXPECT_FALSE(manager.book().initialized());
}