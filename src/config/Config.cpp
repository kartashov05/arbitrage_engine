#include "Config.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace arb {
namespace {

std::string to_upper(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        }
    );

    return value;
}

ExchangeSettings parse_exchange_settings(const YAML::Node& node) {
    ExchangeSettings settings;

    if (!node) {
        return settings;
    }

    settings.enabled = node["enabled"].as<bool>(false);
    settings.ws_base_url = node["ws_base_url"].as<std::string>("");
    settings.rest_base_url = node["rest_base_url"].as<std::string>("");
    settings.max_streams_per_connection =
        node["max_streams_per_connection"].as<std::size_t>(1);

    if (settings.enabled && settings.ws_base_url.empty()) {
        throw std::runtime_error("Enabled exchange must have ws_base_url");
    }

    if (settings.enabled && settings.max_streams_per_connection == 0) {
        throw std::runtime_error("max_streams_per_connection must be greater than zero");
    }

    return settings;
}

}  // namespace

Config load_config(const std::string& path) {
    const YAML::Node root = YAML::LoadFile(path);

    Config config;

    if (root["app"]) {
        config.log_level = root["app"]["log_level"].as<std::string>("info");
    }

    if (root["market_data"]) {
        const auto market_data = root["market_data"];

        config.market_data.update_speed =
            market_data["update_speed"].as<std::string>("100ms");

        config.market_data.book_snapshot_limit =
            market_data["book_snapshot_limit"].as<int>(1000);
    }

    if (!root["symbols"] || !root["symbols"].IsSequence()) {
        throw std::runtime_error("config.yaml must contain symbols list");
    }

    for (const auto& symbol_node : root["symbols"]) {
        std::string symbol = symbol_node.as<std::string>();

        if (symbol.empty()) {
            throw std::runtime_error("Symbol cannot be empty");
        }

        config.symbols.push_back(to_upper(symbol));
    }

    if (config.symbols.empty()) {
        throw std::runtime_error("At least one symbol is required");
    }

    if (root["exchanges"]) {
        config.binance = parse_exchange_settings(root["exchanges"]["binance"]);
        config.mexc = parse_exchange_settings(root["exchanges"]["mexc"]);
    }

    if (root["arbitrage"]) {
        const auto arb = root["arbitrage"];

        config.arbitrage.min_net_spread_bps =
            arb["min_net_spread_bps"].as<int>(10);

        config.arbitrage.max_book_age_ms =
            arb["max_book_age_ms"].as<int>(500);

        config.arbitrage.min_notional_usdt =
            arb["min_notional_usdt"].as<double>(20.0);

        config.arbitrage.max_notional_usdt =
            arb["max_notional_usdt"].as<double>(1000.0);
    }

    if (root["fees"]) {
        config.binance_fees.taker_bps =
            root["fees"]["binance"]["taker_bps"].as<double>(10.0);

        config.mexc_fees.taker_bps =
            root["fees"]["mexc"]["taker_bps"].as<double>(10.0);
    }

    return config;
}

}  // namespace arb