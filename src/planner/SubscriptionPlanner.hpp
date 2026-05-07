#pragma once

#include "../core/Exchange.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace arb {

struct SubscriptionGroup {
    Exchange exchange;
    std::size_t connection_index{};
    std::vector<std::string> symbols;
    std::vector<std::string> streams;

    // Для Binance это будет
    // /stream?streams=btcusdt@depth@100ms/ethusdt@depth@100ms

    // Для MEXC пока оставляем пустым, потому что MEXC подключается к /ws
    // и потом отправляет JSON SUBSCRIPTION message.
    std::string websocket_target;
};

class SubscriptionPlanner {
public:
    std::vector<SubscriptionGroup> plan(
        Exchange exchange,
        const std::vector<std::string>& symbols,
        std::size_t max_streams_per_connection
    ) const;

    static std::string build_stream_name(
        Exchange exchange,
        const std::string& symbol
    );

    static std::string build_binance_combined_target(
        const std::vector<std::string>& streams
    );

private:
    static std::string to_lower(std::string value);
    static std::string to_upper(std::string value);
};

}  // namespace arb