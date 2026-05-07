#include "SubscriptionPlanner.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace arb {

std::vector<SubscriptionGroup> SubscriptionPlanner::plan(
    Exchange exchange,
    const std::vector<std::string>& symbols,
    std::size_t max_streams_per_connection
) const {
    if (max_streams_per_connection == 0) {
        throw std::invalid_argument("max_streams_per_connection must be greater than zero");
    }

    std::vector<SubscriptionGroup> groups;

    if (symbols.empty()) {
        return groups;
    }

    std::size_t connection_index = 0;

    for (std::size_t i = 0; i < symbols.size(); i += max_streams_per_connection) {
        SubscriptionGroup group;
        group.exchange = exchange;
        group.connection_index = connection_index++;

        const std::size_t end = std::min(i + max_streams_per_connection, symbols.size());

        for (std::size_t j = i; j < end; ++j) {
            group.symbols.push_back(symbols[j]);
            group.streams.push_back(build_stream_name(exchange, symbols[j]));
        }

        if (exchange == Exchange::Binance) {
            group.websocket_target = build_binance_combined_target(group.streams);
        }

        groups.push_back(std::move(group));
    }

    return groups;
}

std::string SubscriptionPlanner::build_stream_name(
    Exchange exchange,
    const std::string& symbol
) {
    switch (exchange) {
        case Exchange::Binance:
            return to_lower(symbol) + "@depth@100ms";

        case Exchange::Mexc:
            return "spot@public.aggre.depth.v3.api.pb@100ms@" + to_upper(symbol);
    }

    throw std::runtime_error("Unknown exchange");
}

std::string SubscriptionPlanner::build_binance_combined_target(
    const std::vector<std::string>& streams
) {
    if (streams.empty()) {
        throw std::invalid_argument("Binance combined stream target requires at least one stream");
    }

    std::ostringstream oss;
    oss << "/stream?streams=";

    for (std::size_t i = 0; i < streams.size(); ++i) {
        if (i > 0) {
            oss << "/";
        }

        oss << streams[i];
    }

    return oss.str();
}

std::string SubscriptionPlanner::to_lower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        }
    );

    return value;
}

std::string SubscriptionPlanner::to_upper(std::string value) {
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

}  // namespace arb