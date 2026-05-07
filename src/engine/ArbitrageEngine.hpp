#pragma once

#include "../core/Exchange.hpp"
#include "../core/OrderBook.hpp"

#include <optional>
#include <string>
#include <vector>

namespace arb {

struct ArbitrageEngineSettings {
    double min_net_spread_bps{10.0};
    double min_notional_usdt{20.0};
    double max_notional_usdt{1000.0};

    double binance_taker_fee_bps{10.0};
    double mexc_taker_fee_bps{10.0};

    std::size_t max_depth_levels{50};
};

struct ArbitrageOpportunity {
    std::string symbol;

    Exchange buy_exchange;
    Exchange sell_exchange;

    double base_quantity{};
    double buy_notional_usdt{};
    double sell_notional_usdt{};

    double buy_vwap{};
    double sell_vwap{};

    double gross_profit_usdt{};
    double fees_usdt{};
    double net_profit_usdt{};

    double gross_spread_bps{};
    double total_fee_bps{};
    double net_spread_bps{};
};

class ArbitrageEngine {
public:
    explicit ArbitrageEngine(ArbitrageEngineSettings settings);

    std::vector<ArbitrageOpportunity> scan_symbol(
        const std::string& symbol,
        const OrderBook& binance_book,
        const OrderBook& mexc_book
    ) const;

private:
    std::optional<ArbitrageOpportunity> evaluate_direction(
        const std::string& symbol,
        Exchange buy_exchange,
        Exchange sell_exchange,
        const OrderBook& buy_book,
        const OrderBook& sell_book
    ) const;

    double taker_fee_bps(Exchange exchange) const;

private:
    ArbitrageEngineSettings settings_;
};

}  // namespace arb