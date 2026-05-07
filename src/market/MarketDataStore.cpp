#include "MarketDataStore.hpp"

#include <stdexcept>

namespace arb {

void MarketDataStore::set_book(
    Exchange exchange,
    const std::string& symbol,
    const OrderBook& book
) {
    books_for(exchange)[symbol] = book;
}

const OrderBook* MarketDataStore::get_book(
    Exchange exchange,
    const std::string& symbol
) const {
    const auto& books = books_for(exchange);

    const auto it = books.find(symbol);

    if (it == books.end()) {
        return nullptr;
    }

    return &it->second;
}

bool MarketDataStore::has_book(
    Exchange exchange,
    const std::string& symbol
) const {
    return get_book(exchange, symbol) != nullptr;
}

bool MarketDataStore::has_ready_book(
    Exchange exchange,
    const std::string& symbol
) const {
    const auto* book = get_book(exchange, symbol);

    if (book == nullptr) {
        return false;
    }

    return book->initialized() &&
           book->best_bid().has_value() &&
           book->best_ask().has_value();
}

std::vector<std::string> MarketDataStore::ready_symbols(
    Exchange exchange
) const {
    std::vector<std::string> result;

    const auto& books = books_for(exchange);

    for (const auto& [symbol, book] : books) {
        if (
            book.initialized() &&
            book.best_bid().has_value() &&
            book.best_ask().has_value()
        ) {
            result.push_back(symbol);
        }
    }

    return result;
}

std::size_t MarketDataStore::book_count(
    Exchange exchange
) const {
    return books_for(exchange).size();
}

void MarketDataStore::clear() {
    binance_books_.clear();
    mexc_books_.clear();
}

std::unordered_map<std::string, OrderBook>& MarketDataStore::books_for(
    Exchange exchange
) {
    switch (exchange) {
        case Exchange::Binance:
            return binance_books_;

        case Exchange::Mexc:
            return mexc_books_;
    }

    throw std::runtime_error("Unknown exchange");
}

const std::unordered_map<std::string, OrderBook>& MarketDataStore::books_for(
    Exchange exchange
) const {
    switch (exchange) {
        case Exchange::Binance:
            return binance_books_;

        case Exchange::Mexc:
            return mexc_books_;
    }

    throw std::runtime_error("Unknown exchange");
}

}  // namespace arb