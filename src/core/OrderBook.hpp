#pragma once

#include "BookUpdate.hpp"
#include "PriceLevel.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace arb {

enum class ApplyUpdateResult {
    Applied,
    IgnoredStale,
    GapDetected,
    NotInitialized
};

class OrderBook {
public:
    void apply_snapshot(
        const std::vector<PriceLevel>& bids,
        const std::vector<PriceLevel>& asks,
        std::uint64_t last_update_id
    );

    ApplyUpdateResult apply_update(const BookUpdate& update);

    [[nodiscard]] std::optional<PriceLevel> best_bid() const;
    [[nodiscard]] std::optional<PriceLevel> best_ask() const;

    [[nodiscard]] std::vector<PriceLevel> top_bids(std::size_t limit) const;
    [[nodiscard]] std::vector<PriceLevel> top_asks(std::size_t limit) const;

    [[nodiscard]] bool initialized() const;
    [[nodiscard]] std::uint64_t last_update_id() const;

    [[nodiscard]] std::size_t bid_levels() const;
    [[nodiscard]] std::size_t ask_levels() const;

    void clear();

private:
    static void apply_levels_to_bids(
        std::map<double, double, std::greater<>>& bids,
        const std::vector<PriceLevel>& levels
    );

    static void apply_levels_to_asks(
        std::map<double, double>& asks,
        const std::vector<PriceLevel>& levels
    );

private:
    std::map<double, double, std::greater<>> bids_;
    std::map<double, double> asks_;

    std::uint64_t last_update_id_{0};
    bool initialized_{false};
};

}  // namespace arb