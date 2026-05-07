#pragma once

#include "../core/BookSnapshot.hpp"
#include "../core/BookUpdate.hpp"
#include "../core/OrderBook.hpp"

#include <cstddef>
#include <deque>
#include <string>

namespace arb {

enum class MexcLocalBookStatus {
    WaitingForSnapshot,
    Ready,
    NeedResync
};

enum class MexcLocalBookUpdateResult {
    Buffered,
    SnapshotApplied,
    SnapshotRejected,
    Applied,
    IgnoredStale,
    GapDetected,
    NotForSymbol
};

struct MexcLocalBookStats {
    std::size_t buffered_updates{};
    std::size_t applied_updates{};
    std::size_t ignored_stale_updates{};
    std::size_t gaps_detected{};
};

class MexcLocalBookManager {
public:
    explicit MexcLocalBookManager(
        std::string symbol,
        std::size_t max_buffered_updates = 10000
    );

    MexcLocalBookUpdateResult on_update(const BookUpdate& update);
    MexcLocalBookUpdateResult apply_snapshot(const BookSnapshot& snapshot);

    [[nodiscard]] const OrderBook& book() const;

    [[nodiscard]] const std::string& symbol() const;
    [[nodiscard]] MexcLocalBookStatus status() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool need_resync() const;

    [[nodiscard]] std::size_t buffered_updates() const;
    [[nodiscard]] MexcLocalBookStats stats() const;

    void reset();

private:
    [[nodiscard]] bool belongs_to_this_book(const BookUpdate& update) const;
    [[nodiscard]] bool belongs_to_this_book(const BookSnapshot& snapshot) const;

    [[nodiscard]] MexcLocalBookUpdateResult apply_update_to_ready_book(
        const BookUpdate& update
    );

private:
    std::string symbol_;
    std::size_t max_buffered_updates_{};

    OrderBook book_;
    std::deque<BookUpdate> buffer_;

    MexcLocalBookStatus status_{MexcLocalBookStatus::WaitingForSnapshot};
    MexcLocalBookStats stats_;
};

}  // namespace arb