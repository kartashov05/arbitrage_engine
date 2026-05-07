#include "ArbitrageEngine.hpp"

#include <algorithm>
#include <cmath>

namespace arb {
namespace {

constexpr double kEpsilon = 1e-12;
constexpr double kBpsMultiplier = 10000.0;

}  // namespace

ArbitrageEngine::ArbitrageEngine(ArbitrageEngineSettings settings)
    : settings_(settings) {}

std::vector<ArbitrageOpportunity> ArbitrageEngine::scan_symbol(
    const std::string& symbol,
    const OrderBook& binance_book,
    const OrderBook& mexc_book
) const {
    std::vector<ArbitrageOpportunity> opportunities;

    if (!binance_book.initialized() || !mexc_book.initialized()) {
        return opportunities;
    }

    if (auto opportunity = evaluate_direction(
            symbol,
            Exchange::Binance,
            Exchange::Mexc,
            binance_book,
            mexc_book
        )) {
        opportunities.push_back(*opportunity);
    }

    if (auto opportunity = evaluate_direction(
            symbol,
            Exchange::Mexc,
            Exchange::Binance,
            mexc_book,
            binance_book
        )) {
        opportunities.push_back(*opportunity);
    }

    std::sort(
        opportunities.begin(),
        opportunities.end(),
        [](const auto& left, const auto& right) {
            return left.net_spread_bps > right.net_spread_bps;
        }
    );

    return opportunities;
}

std::optional<ArbitrageOpportunity> ArbitrageEngine::evaluate_direction(
    const std::string& symbol,
    Exchange buy_exchange,
    Exchange sell_exchange,
    const OrderBook& buy_book,
    const OrderBook& sell_book
) const {
    const auto asks = buy_book.top_asks(settings_.max_depth_levels);
    const auto bids = sell_book.top_bids(settings_.max_depth_levels);

    if (asks.empty() || bids.empty()) {
        return std::nullopt;
    }

    std::size_t ask_index = 0;
    std::size_t bid_index = 0;

    double ask_remaining_qty = asks[ask_index].quantity;
    double bid_remaining_qty = bids[bid_index].quantity;

    double remaining_buy_notional_cap = settings_.max_notional_usdt;

    double base_quantity = 0.0;
    double buy_notional = 0.0;
    double sell_notional = 0.0;

    while (
        ask_index < asks.size() &&
        bid_index < bids.size() &&
        remaining_buy_notional_cap > kEpsilon
    ) {
        const double ask_price = asks[ask_index].price;
        const double bid_price = bids[bid_index].price;

        if (ask_price <= 0.0 || bid_price <= 0.0) {
            break;
        }

        // Если даже маржинально продать уже нельзя дороже покупки,
        // дальше по стакану будет только хуже:
        // asks растут, bids падают.
        if (bid_price <= ask_price) {
            break;
        }

        const double qty_by_notional_cap = remaining_buy_notional_cap / ask_price;

        const double executable_qty = std::min({
            ask_remaining_qty,
            bid_remaining_qty,
            qty_by_notional_cap
        });

        if (executable_qty <= kEpsilon) {
            break;
        }

        base_quantity += executable_qty;
        buy_notional += executable_qty * ask_price;
        sell_notional += executable_qty * bid_price;

        remaining_buy_notional_cap -= executable_qty * ask_price;

        ask_remaining_qty -= executable_qty;
        bid_remaining_qty -= executable_qty;

        if (ask_remaining_qty <= kEpsilon) {
            ++ask_index;

            if (ask_index < asks.size()) {
                ask_remaining_qty = asks[ask_index].quantity;
            }
        }

        if (bid_remaining_qty <= kEpsilon) {
            ++bid_index;

            if (bid_index < bids.size()) {
                bid_remaining_qty = bids[bid_index].quantity;
            }
        }
    }

    if (base_quantity <= kEpsilon || buy_notional <= kEpsilon) {
        return std::nullopt;
    }

    if (buy_notional < settings_.min_notional_usdt) {
        return std::nullopt;
    }

    const double buy_fee = buy_notional * taker_fee_bps(buy_exchange) / kBpsMultiplier;
    const double sell_fee = sell_notional * taker_fee_bps(sell_exchange) / kBpsMultiplier;

    const double gross_profit = sell_notional - buy_notional;
    const double fees = buy_fee + sell_fee;
    const double net_profit = gross_profit - fees;

    const double buy_vwap = buy_notional / base_quantity;
    const double sell_vwap = sell_notional / base_quantity;

    const double gross_spread_bps = gross_profit / buy_notional * kBpsMultiplier;
    const double total_fee_bps = fees / buy_notional * kBpsMultiplier;
    const double net_spread_bps = net_profit / buy_notional * kBpsMultiplier;

    if (net_spread_bps < settings_.min_net_spread_bps) {
        return std::nullopt;
    }

    return ArbitrageOpportunity{
        .symbol = symbol,
        .buy_exchange = buy_exchange,
        .sell_exchange = sell_exchange,
        .base_quantity = base_quantity,
        .buy_notional_usdt = buy_notional,
        .sell_notional_usdt = sell_notional,
        .buy_vwap = buy_vwap,
        .sell_vwap = sell_vwap,
        .gross_profit_usdt = gross_profit,
        .fees_usdt = fees,
        .net_profit_usdt = net_profit,
        .gross_spread_bps = gross_spread_bps,
        .total_fee_bps = total_fee_bps,
        .net_spread_bps = net_spread_bps
    };
}

double ArbitrageEngine::taker_fee_bps(Exchange exchange) const {
    switch (exchange) {
        case Exchange::Binance:
            return settings_.binance_taker_fee_bps;

        case Exchange::Mexc:
            return settings_.mexc_taker_fee_bps;
    }

    return 0.0;
}

}  // namespace arb