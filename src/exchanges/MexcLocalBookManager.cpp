#include "MexcLocalBookManager.hpp"

#include <utility>

namespace arb {

MexcLocalBookManager::MexcLocalBookManager(
    std::string symbol,
    std::size_t max_buffered_updates
)
    : symbol_(std::move(symbol)),
      max_buffered_updates_(max_buffered_updates) {}

MexcLocalBookUpdateResult MexcLocalBookManager::on_update(
    const BookUpdate& update
) {
    if (!belongs_to_this_book(update)) {
        return MexcLocalBookUpdateResult::NotForSymbol;
    }

    if (status_ == MexcLocalBookStatus::NeedResync) {
        return MexcLocalBookUpdateResult::GapDetected;
    }

    if (status_ == MexcLocalBookStatus::WaitingForSnapshot) {
        buffer_.push_back(update);
        stats_.buffered_updates = buffer_.size();

        if (buffer_.size() > max_buffered_updates_) {
            buffer_.pop_front();
            stats_.buffered_updates = buffer_.size();
        }

        return MexcLocalBookUpdateResult::Buffered;
    }

    return apply_update_to_ready_book(update);
}

MexcLocalBookUpdateResult MexcLocalBookManager::apply_snapshot(
    const BookSnapshot& snapshot
) {
    if (!belongs_to_this_book(snapshot)) {
        return MexcLocalBookUpdateResult::NotForSymbol;
    }

    book_.apply_snapshot(
        snapshot.bids,
        snapshot.asks,
        snapshot.last_update_id
    );

    while (
        !buffer_.empty() &&
        buffer_.front().final_update_id <= snapshot.last_update_id
    ) {
        buffer_.pop_front();
        ++stats_.ignored_stale_updates;
    }

    stats_.buffered_updates = buffer_.size();

    if (buffer_.empty()) {
        status_ = MexcLocalBookStatus::Ready;
        return MexcLocalBookUpdateResult::SnapshotApplied;
    }

    const auto expected_next_update_id = snapshot.last_update_id + 1;
    const auto& first_update = buffer_.front();

    if (
        first_update.first_update_id > expected_next_update_id ||
        first_update.final_update_id < expected_next_update_id
    ) {
        status_ = MexcLocalBookStatus::NeedResync;
        buffer_.clear();
        stats_.buffered_updates = 0;
        ++stats_.gaps_detected;

        return MexcLocalBookUpdateResult::SnapshotRejected;
    }

    while (!buffer_.empty()) {
        const auto result = apply_update_to_ready_book(buffer_.front());

        if (result == MexcLocalBookUpdateResult::GapDetected) {
            buffer_.clear();
            stats_.buffered_updates = 0;
            return result;
        }

        buffer_.pop_front();
        stats_.buffered_updates = buffer_.size();
    }

    status_ = MexcLocalBookStatus::Ready;
    return MexcLocalBookUpdateResult::SnapshotApplied;
}

const OrderBook& MexcLocalBookManager::book() const {
    return book_;
}

const std::string& MexcLocalBookManager::symbol() const {
    return symbol_;
}

MexcLocalBookStatus MexcLocalBookManager::status() const {
    return status_;
}

bool MexcLocalBookManager::ready() const {
    return status_ == MexcLocalBookStatus::Ready;
}

bool MexcLocalBookManager::need_resync() const {
    return status_ == MexcLocalBookStatus::NeedResync;
}

std::size_t MexcLocalBookManager::buffered_updates() const {
    return buffer_.size();
}

MexcLocalBookStats MexcLocalBookManager::stats() const {
    return stats_;
}

void MexcLocalBookManager::reset() {
    book_.clear();
    buffer_.clear();

    status_ = MexcLocalBookStatus::WaitingForSnapshot;
    stats_ = MexcLocalBookStats{};
}

bool MexcLocalBookManager::belongs_to_this_book(
    const BookUpdate& update
) const {
    return update.exchange == Exchange::Mexc && update.symbol == symbol_;
}

bool MexcLocalBookManager::belongs_to_this_book(
    const BookSnapshot& snapshot
) const {
    return snapshot.exchange == Exchange::Mexc && snapshot.symbol == symbol_;
}

MexcLocalBookUpdateResult MexcLocalBookManager::apply_update_to_ready_book(
    const BookUpdate& update
) {
    const auto result = book_.apply_update(update);

    switch (result) {
        case ApplyUpdateResult::Applied:
            status_ = MexcLocalBookStatus::Ready;
            ++stats_.applied_updates;
            return MexcLocalBookUpdateResult::Applied;

        case ApplyUpdateResult::IgnoredStale:
            ++stats_.ignored_stale_updates;
            return MexcLocalBookUpdateResult::IgnoredStale;

        case ApplyUpdateResult::GapDetected:
            status_ = MexcLocalBookStatus::NeedResync;
            ++stats_.gaps_detected;
            return MexcLocalBookUpdateResult::GapDetected;

        case ApplyUpdateResult::NotInitialized:
            status_ = MexcLocalBookStatus::WaitingForSnapshot;
            return MexcLocalBookUpdateResult::SnapshotRejected;
    }

    return MexcLocalBookUpdateResult::SnapshotRejected;
}

}  // namespace arb