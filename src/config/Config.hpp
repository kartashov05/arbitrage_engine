#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace arb {

struct ExchangeSettings {
    bool enabled{false};
    std::string ws_base_url;
    std::string rest_base_url;
    std::size_t max_streams_per_connection{1};
};

struct MarketDataSettings {
    std::string update_speed{"100ms"};
    int book_snapshot_limit{1000};
};

struct ArbitrageSettings {
    int min_net_spread_bps{10};
    int max_book_age_ms{500};
    double min_notional_usdt{20.0};
    double max_notional_usdt{1000.0};
};

struct FeeSettings {
    double taker_bps{10.0};
};

struct Config {
    std::string log_level{"info"};

    MarketDataSettings market_data;

    std::vector<std::string> symbols;

    ExchangeSettings binance;
    ExchangeSettings mexc;

    ArbitrageSettings arbitrage;

    FeeSettings binance_fees;
    FeeSettings mexc_fees;
};

Config load_config(const std::string& path);

}  // namespace arb