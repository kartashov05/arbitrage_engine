#include "BinanceLocalBookManager.hpp"

#include <utility>

namespace arb {

BinanceLocalBookManager::BinanceLocalBookManager(
    std::string symbol,
    std::size_t max_buffered_updates
)
    : symbol_(std::move(symbol)),
      max_buffered_updates_(max_buffered_updates) {}

LocalBookUpdateResult BinanceLocalBookManager::on_update(
    const BookUpdate& update
) {
    if (!belongs_to_this_book(update)) {
        return LocalBookUpdateResult::NotForSymbol;
    }

    if (status_ == LocalBookStatus::NeedResync) {
        return LocalBookUpdateResult::GapDetected;
    }

    if (status_ == LocalBookStatus::WaitingForSnapshot) {
        buffer_.push_back(update);
        stats_.buffered_updates = buffer_.size();

        if (buffer_.size() > max_buffered_updates_) {
            buffer_.pop_front();
            stats_.buffered_updates = buffer_.size();
        }

        return LocalBookUpdateResult::Buffered;
    }

    return apply_update_to_ready_book(update);
}

LocalBookUpdateResult BinanceLocalBookManager::apply_snapshot(
    const BookSnapshot& snapshot
) {
    if (!belongs_to_this_book(snapshot)) {
        return LocalBookUpdateResult::NotForSymbol;
    }

    book_.apply_snapshot(
        snapshot.bids,
        snapshot.asks,
        snapshot.last_update_id
    );

    // Drop updates that are older than or equal to snapshot
    while (
        !buffer_.empty() &&
        buffer_.front().final_update_id <= snapshot.last_update_id
    ) {
        buffer_.pop_front();
        ++stats_.ignored_stale_updates;
    }

    stats_.buffered_updates = buffer_.size();

    // This can happen if snapshot is newer than all buffered updates
    // Then we can continue with future WS updates
    if (buffer_.empty()) {
        status_ = LocalBookStatus::Ready;
        return LocalBookUpdateResult::SnapshotApplied;
    }

    const auto expected_next_update_id = snapshot.last_update_id + 1;
    const auto& first_update = buffer_.front();

    // Binance rule:
    // first processed event should satisfy:
    // U <= lastUpdateId + 1 <= u
    if (
        first_update.first_update_id > expected_next_update_id ||
        first_update.final_update_id < expected_next_update_id
    ) {
        status_ = LocalBookStatus::NeedResync;
        buffer_.clear();
        stats_.buffered_updates = 0;
        ++stats_.gaps_detected;

        return LocalBookUpdateResult::SnapshotRejected;
    }

    while (!buffer_.empty()) {
        const auto result = apply_update_to_ready_book(buffer_.front());

        if (result == LocalBookUpdateResult::GapDetected) {
            buffer_.clear();
            stats_.buffered_updates = 0;
            return result;
        }

        buffer_.pop_front();
        stats_.buffered_updates = buffer_.size();
    }

    status_ = LocalBookStatus::Ready;
    return LocalBookUpdateResult::SnapshotApplied;
}

const OrderBook& BinanceLocalBookManager::book() const {
    return book_;
}

const std::string& BinanceLocalBookManager::symbol() const {
    return symbol_;
}

LocalBookStatus BinanceLocalBookManager::status() const {
    return status_;
}

bool BinanceLocalBookManager::ready() const {
    return status_ == LocalBookStatus::Ready;
}

bool BinanceLocalBookManager::need_resync() const {
    return status_ == LocalBookStatus::NeedResync;
}

std::size_t BinanceLocalBookManager::buffered_updates() const {
    return buffer_.size();
}

LocalBookStats BinanceLocalBookManager::stats() const {
    return stats_;
}

void BinanceLocalBookManager::reset() {
    book_.clear();
    buffer_.clear();

    status_ = LocalBookStatus::WaitingForSnapshot;
    stats_ = LocalBookStats{};
}

bool BinanceLocalBookManager::belongs_to_this_book(
    const BookUpdate& update
) const {
    return update.exchange == Exchange::Binance && update.symbol == symbol_;
}

bool BinanceLocalBookManager::belongs_to_this_book(
    const BookSnapshot& snapshot
) const {
    return snapshot.exchange == Exchange::Binance && snapshot.symbol == symbol_;
}

LocalBookUpdateResult BinanceLocalBookManager::apply_update_to_ready_book(
    const BookUpdate& update
) {
    const auto result = book_.apply_update(update);

    switch (result) {
        case ApplyUpdateResult::Applied:
            status_ = LocalBookStatus::Ready;
            ++stats_.applied_updates;
            return LocalBookUpdateResult::Applied;

        case ApplyUpdateResult::IgnoredStale:
            ++stats_.ignored_stale_updates;
            return LocalBookUpdateResult::IgnoredStale;

        case ApplyUpdateResult::GapDetected:
            status_ = LocalBookStatus::NeedResync;
            ++stats_.gaps_detected;
            return LocalBookUpdateResult::GapDetected;

        case ApplyUpdateResult::NotInitialized:
            status_ = LocalBookStatus::WaitingForSnapshot;
            return LocalBookUpdateResult::SnapshotRejected;
    }

    return LocalBookUpdateResult::SnapshotRejected;
}

}  // namespace arb