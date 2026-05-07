#pragma once

#include "../core/Exchange.hpp"
#include "../core/OrderBook.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace arb {

class MarketDataStore {
public:
    void set_book(
        Exchange exchange,
        const std::string& symbol,
        const OrderBook& book
    );

    [[nodiscard]] const OrderBook* get_book(
        Exchange exchange,
        const std::string& symbol
    ) const;

    [[nodiscard]] bool has_book(
        Exchange exchange,
        const std::string& symbol
    ) const;

    [[nodiscard]] bool has_ready_book(
        Exchange exchange,
        const std::string& symbol
    ) const;

    [[nodiscard]] std::vector<std::string> ready_symbols(
        Exchange exchange
    ) const;

    [[nodiscard]] std::size_t book_count(
        Exchange exchange
    ) const;

    void clear();

private:
    [[nodiscard]] std::unordered_map<std::string, OrderBook>& books_for(
        Exchange exchange
    );

    [[nodiscard]] const std::unordered_map<std::string, OrderBook>& books_for(
        Exchange exchange
    ) const;

private:
    std::unordered_map<std::string, OrderBook> binance_books_;
    std::unordered_map<std::string, OrderBook> mexc_books_;
};

}  // namespace arb