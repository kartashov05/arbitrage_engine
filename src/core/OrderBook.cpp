#include "OrderBook.hpp"

namespace arb {

void OrderBook::apply_snapshot(
    const std::vector<PriceLevel>& bids,
    const std::vector<PriceLevel>& asks,
    std::uint64_t last_update_id
) {
    bids_.clear();
    asks_.clear();

    apply_levels_to_bids(bids_, bids);
    apply_levels_to_asks(asks_, asks);

    last_update_id_ = last_update_id;
    initialized_ = true;
}

ApplyUpdateResult OrderBook::apply_update(const BookUpdate& update) {
    if (!initialized_) {
        return ApplyUpdateResult::NotInitialized;
    }

    if (update.final_update_id <= last_update_id_) {
        return ApplyUpdateResult::IgnoredStale;
    }

    if (update.first_update_id > last_update_id_ + 1) {
        return ApplyUpdateResult::GapDetected;
    }

    apply_levels_to_bids(bids_, update.bids);
    apply_levels_to_asks(asks_, update.asks);

    last_update_id_ = update.final_update_id;

    return ApplyUpdateResult::Applied;
}

std::optional<PriceLevel> OrderBook::best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }

    const auto& [price, quantity] = *bids_.begin();

    return PriceLevel{
        .price = price,
        .quantity = quantity
    };
}

std::optional<PriceLevel> OrderBook::best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }

    const auto& [price, quantity] = *asks_.begin();

    return PriceLevel{
        .price = price,
        .quantity = quantity
    };
}

std::vector<PriceLevel> OrderBook::top_bids(std::size_t limit) const {
    std::vector<PriceLevel> result;
    result.reserve(std::min(limit, bids_.size()));

    std::size_t count = 0;

    for (const auto& [price, quantity] : bids_) {
        if (count >= limit) {
            break;
        }

        result.push_back(PriceLevel{
            .price = price,
            .quantity = quantity
        });

        ++count;
    }

    return result;
}

std::vector<PriceLevel> OrderBook::top_asks(std::size_t limit) const {
    std::vector<PriceLevel> result;
    result.reserve(std::min(limit, asks_.size()));

    std::size_t count = 0;

    for (const auto& [price, quantity] : asks_) {
        if (count >= limit) {
            break;
        }

        result.push_back(PriceLevel{
            .price = price,
            .quantity = quantity
        });

        ++count;
    }

    return result;
}

bool OrderBook::initialized() const {
    return initialized_;
}

std::uint64_t OrderBook::last_update_id() const {
    return last_update_id_;
}

std::size_t OrderBook::bid_levels() const {
    return bids_.size();
}

std::size_t OrderBook::ask_levels() const {
    return asks_.size();
}

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
    last_update_id_ = 0;
    initialized_ = false;
}

void OrderBook::apply_levels_to_bids(
    std::map<double, double, std::greater<>>& bids,
    const std::vector<PriceLevel>& levels
) {
    for (const auto& level : levels) {
        if (level.quantity <= 0.0) {
            bids.erase(level.price);
        } else {
            bids[level.price] = level.quantity;
        }
    }
}

void OrderBook::apply_levels_to_asks(
    std::map<double, double>& asks,
    const std::vector<PriceLevel>& levels
) {
    for (const auto& level : levels) {
        if (level.quantity <= 0.0) {
            asks.erase(level.price);
        } else {
            asks[level.price] = level.quantity;
        }
    }
}

}  // namespace arb