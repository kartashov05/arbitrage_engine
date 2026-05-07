#include "config/Config.hpp"
#include "core/OrderBook.hpp"
#include "core/Exchange.hpp"
#include "exchanges/BinanceDepthParser.hpp"
#include "exchanges/BinanceSnapshotParser.hpp"
#include "exchanges/BinanceLocalBookManager.hpp"
#include "exchanges/MexcSnapshotParser.hpp"
#include "exchanges/MexcDepthParser.hpp"
#include "exchanges/MexcLocalBookManager.hpp"
#include "engine/ArbitrageEngine.hpp"
#include "market/MarketDataStore.hpp"
#include "net/WebSocketClient.hpp"
#include "net/WsEndpoint.hpp"
#include "net/HttpsClient.hpp"
#include "net/HttpsEndpoint.hpp"
#include "planner/SubscriptionPlanner.hpp"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <exception>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <optional>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace {

void configure_logger(const std::string& level) {
    if (level == "trace") {
        spdlog::set_level(spdlog::level::trace);
    } else if (level == "debug") {
        spdlog::set_level(spdlog::level::debug);
    } else if (level == "info") {
        spdlog::set_level(spdlog::level::info);
    } else if (level == "warn") {
        spdlog::set_level(spdlog::level::warn);
    } else if (level == "error") {
        spdlog::set_level(spdlog::level::err);
    } else {
        spdlog::set_level(spdlog::level::info);
    }
}

bool has_arg(int argc, char** argv, const std::string& value) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == value) {
            return true;
        }
    }

    return false;
}

std::optional<std::string> get_arg_value(
    int argc,
    char** argv,
    const std::string& prefix
) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg.rfind(prefix, 0) == 0) {
            return arg.substr(prefix.size());
        }
    }

    return std::nullopt;
}

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

std::size_t get_max_messages_arg(int argc, char** argv) {
    const std::string prefix = "--max-messages=";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg.rfind(prefix, 0) == 0) {
            return static_cast<std::size_t>(
                std::stoull(arg.substr(prefix.size()))
            );
        }
    }

    return 0;
}

std::string get_config_path(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg.rfind("--", 0) != 0) {
            return arg;
        }
    }

    return "config/config.yaml";
}


struct TopOfBookSpread {
    std::string symbol;

    double binance_bid_price{};
    double binance_bid_quantity{};
    double binance_ask_price{};
    double binance_ask_quantity{};

    double mexc_bid_price{};
    double mexc_bid_quantity{};
    double mexc_ask_price{};
    double mexc_ask_quantity{};

    arb::Exchange buy_exchange{};
    arb::Exchange sell_exchange{};

    double gross_spread_bps{};
    double total_fee_bps{};
    double net_spread_bps{};
};

std::optional<TopOfBookSpread> compute_top_of_book_spread(
    const std::string& symbol,
    const arb::OrderBook& binance_book,
    const arb::OrderBook& mexc_book,
    const arb::Config& config
) {
    const auto binance_bid = binance_book.best_bid();
    const auto binance_ask = binance_book.best_ask();

    const auto mexc_bid = mexc_book.best_bid();
    const auto mexc_ask = mexc_book.best_ask();

    if (
        !binance_bid.has_value() ||
        !binance_ask.has_value() ||
        !mexc_bid.has_value() ||
        !mexc_ask.has_value()
    ) {
        return std::nullopt;
    }

    if (binance_ask->price <= 0.0 || mexc_ask->price <= 0.0) {
        return std::nullopt;
    }

    const double total_fee_bps =
        config.binance_fees.taker_bps +
        config.mexc_fees.taker_bps;

    const double buy_binance_sell_mexc_gross_bps =
        ((mexc_bid->price - binance_ask->price) / binance_ask->price) *
        10000.0;

    const double buy_mexc_sell_binance_gross_bps =
        ((binance_bid->price - mexc_ask->price) / mexc_ask->price) *
        10000.0;

    TopOfBookSpread spread{
        .symbol = symbol,

        .binance_bid_price = binance_bid->price,
        .binance_bid_quantity = binance_bid->quantity,
        .binance_ask_price = binance_ask->price,
        .binance_ask_quantity = binance_ask->quantity,

        .mexc_bid_price = mexc_bid->price,
        .mexc_bid_quantity = mexc_bid->quantity,
        .mexc_ask_price = mexc_ask->price,
        .mexc_ask_quantity = mexc_ask->quantity,

        .buy_exchange = arb::Exchange::Binance,
        .sell_exchange = arb::Exchange::Mexc,

        .gross_spread_bps = buy_binance_sell_mexc_gross_bps,
        .total_fee_bps = total_fee_bps,
        .net_spread_bps = buy_binance_sell_mexc_gross_bps - total_fee_bps
    };

    const double buy_mexc_sell_binance_net_bps =
        buy_mexc_sell_binance_gross_bps - total_fee_bps;

    if (buy_mexc_sell_binance_net_bps > spread.net_spread_bps) {
        spread.buy_exchange = arb::Exchange::Mexc;
        spread.sell_exchange = arb::Exchange::Binance;
        spread.gross_spread_bps = buy_mexc_sell_binance_gross_bps;
        spread.net_spread_bps = buy_mexc_sell_binance_net_bps;
    }

    return spread;
}

void print_top_of_book_spread(
    const TopOfBookSpread& spread
) {
    if (spread.net_spread_bps > 0.0) {
        spdlog::warn(
            "[top-spread] symbol={} buy={} sell={} gross_bps={} fee_bps={} net_bps={} "
            "binance_bid={}@{} binance_ask={}@{} mexc_bid={}@{} mexc_ask={}@{}",
            spread.symbol,
            arb::to_string(spread.buy_exchange),
            arb::to_string(spread.sell_exchange),
            spread.gross_spread_bps,
            spread.total_fee_bps,
            spread.net_spread_bps,
            spread.binance_bid_quantity,
            spread.binance_bid_price,
            spread.binance_ask_quantity,
            spread.binance_ask_price,
            spread.mexc_bid_quantity,
            spread.mexc_bid_price,
            spread.mexc_ask_quantity,
            spread.mexc_ask_price
        );
    } else {
        spdlog::info(
            "[top-spread] symbol={} buy={} sell={} gross_bps={} fee_bps={} net_bps={} "
            "binance_bid={}@{} binance_ask={}@{} mexc_bid={}@{} mexc_ask={}@{}",
            spread.symbol,
            arb::to_string(spread.buy_exchange),
            arb::to_string(spread.sell_exchange),
            spread.gross_spread_bps,
            spread.total_fee_bps,
            spread.net_spread_bps,
            spread.binance_bid_quantity,
            spread.binance_bid_price,
            spread.binance_ask_quantity,
            spread.binance_ask_price,
            spread.mexc_bid_quantity,
            spread.mexc_bid_price,
            spread.mexc_ask_quantity,
            spread.mexc_ask_price
        );
    }
}

std::string build_mexc_subscription_message(
    const std::vector<std::string>& streams
) {
    nlohmann::json request;

    request["method"] = "SUBSCRIPTION";
    request["params"] = streams;

    return request.dump();
}

arb::ArbitrageEngineSettings make_arbitrage_settings(
    const arb::Config& config
) {
    return arb::ArbitrageEngineSettings{
        .min_net_spread_bps =
            static_cast<double>(config.arbitrage.min_net_spread_bps),
        .min_notional_usdt = config.arbitrage.min_notional_usdt,
        .max_notional_usdt = config.arbitrage.max_notional_usdt,
        .binance_taker_fee_bps = config.binance_fees.taker_bps,
        .mexc_taker_fee_bps = config.mexc_fees.taker_bps,
        .max_depth_levels = 50
    };
}

void print_arbitrage_opportunity(
    const arb::ArbitrageOpportunity& opportunity
) {
    spdlog::warn(
        "[arbitrage] symbol={} buy={} sell={} qty={} buy_vwap={} sell_vwap={} gross_bps={} fee_bps={} net_bps={} net_profit_usdt={}",
        opportunity.symbol,
        arb::to_string(opportunity.buy_exchange),
        arb::to_string(opportunity.sell_exchange),
        opportunity.base_quantity,
        opportunity.buy_vwap,
        opportunity.sell_vwap,
        opportunity.gross_spread_bps,
        opportunity.total_fee_bps,
        opportunity.net_spread_bps,
        opportunity.net_profit_usdt
    );
}

void scan_live_store_once(
    const arb::Config& config,
    const arb::MarketDataStore& store,
    const arb::ArbitrageEngine& engine,
    std::size_t scan_index
) {
    std::vector<TopOfBookSpread> spreads;
    std::vector<arb::ArbitrageOpportunity> opportunities;

    std::size_t ready_pairs = 0;

    for (const auto& symbol : config.symbols) {
        const auto* binance_book = store.get_book(
            arb::Exchange::Binance,
            symbol
        );

        const auto* mexc_book = store.get_book(
            arb::Exchange::Mexc,
            symbol
        );

        if (binance_book == nullptr || mexc_book == nullptr) {
            continue;
        }

        if (
            !binance_book->initialized() ||
            !mexc_book->initialized()
        ) {
            continue;
        }

        ++ready_pairs;

        const auto top_spread = compute_top_of_book_spread(
            symbol,
            *binance_book,
            *mexc_book,
            config
        );

        if (top_spread.has_value()) {
            spreads.push_back(*top_spread);
        }

        auto symbol_opportunities = engine.scan_symbol(
            symbol,
            *binance_book,
            *mexc_book
        );

        opportunities.insert(
            opportunities.end(),
            symbol_opportunities.begin(),
            symbol_opportunities.end()
        );
    }

    std::sort(
        spreads.begin(),
        spreads.end(),
        [](const auto& left, const auto& right) {
            return left.net_spread_bps > right.net_spread_bps;
        }
    );

    std::sort(
        opportunities.begin(),
        opportunities.end(),
        [](const auto& left, const auto& right) {
            return left.net_spread_bps > right.net_spread_bps;
        }
    );

    spdlog::info(
        "Live scan #{}: ready_pairs={} top_spreads={} opportunities={}",
        scan_index,
        ready_pairs,
        spreads.size(),
        opportunities.size()
    );

    const std::size_t spreads_to_print =
        std::min<std::size_t>(3, spreads.size());

    for (std::size_t i = 0; i < spreads_to_print; ++i) {
        print_top_of_book_spread(spreads[i]);
    }

    if (opportunities.empty()) {
        return;
    }

    const std::size_t opportunities_to_print =
        std::min<std::size_t>(10, opportunities.size());

    for (std::size_t i = 0; i < opportunities_to_print; ++i) {
        print_arbitrage_opportunity(opportunities[i]);
    }
}

void print_groups(
    const std::string& exchange_name,
    const std::vector<arb::SubscriptionGroup>& groups
) {
    spdlog::info("{} subscription groups: {}", exchange_name, groups.size());

    for (const auto& group : groups) {
        spdlog::info(
            "{} socket #{}: {} symbols, {} streams",
            exchange_name,
            group.connection_index + 1,
            group.symbols.size(),
            group.streams.size()
        );

        if (!group.websocket_target.empty()) {
            spdlog::info("  target: {}", group.websocket_target);
        }

        for (const auto& stream : group.streams) {
            spdlog::debug("  stream: {}", stream);
        }
    }
}

void run_binance_live(
    const arb::Config& config,
    const std::vector<arb::SubscriptionGroup>& binance_groups,
    std::size_t max_messages
) {
    if (binance_groups.empty()) {
        spdlog::warn("No Binance subscription groups to run");
        return;
    }

    const auto& group = binance_groups.front();

    spdlog::warn(
        "Starting Binance live mode for socket #{} only. Press Ctrl+C to stop.",
        group.connection_index + 1
    );

    spdlog::info("Binance target: {}", group.websocket_target);

    const auto endpoint = arb::make_ws_endpoint(
        config.binance.ws_base_url,
        group.websocket_target
    );

    arb::WebSocketClient client;
    arb::BinanceDepthParser parser;

    std::size_t parsed_depth_updates = 0;
    std::size_t raw_messages = 0;

    client.run(
        endpoint,
        [&](std::string_view message) {
            ++raw_messages;

            try {
                const auto update = parser.parse(message);

                if (!update.has_value()) {
                    spdlog::debug("Ignored non-depth message");
                    return;
                }

                ++parsed_depth_updates;

                spdlog::info(
                    "[{}] depth update #{} symbol={} U={} u={} bids={} asks={} event_latency_ms={}",
                    arb::to_string(update->exchange),
                    parsed_depth_updates,
                    update->symbol,
                    update->first_update_id,
                    update->final_update_id,
                    update->bids.size(),
                    update->asks.size(),
                    update->local_receive_time_ms - update->exchange_event_time_ms
                );

                if (!update->bids.empty()) {
                    spdlog::debug(
                        "  first bid: price={} quantity={}",
                        update->bids.front().price,
                        update->bids.front().quantity
                    );
                }

                if (!update->asks.empty()) {
                    spdlog::debug(
                        "  first ask: price={} quantity={}",
                        update->asks.front().price,
                        update->asks.front().quantity
                    );
                }

            } catch (const std::exception& e) {
                spdlog::error("Failed to parse Binance message: {}", e.what());
            }
        },
        max_messages
    );

    spdlog::info(
        "Binance live stopped. raw_messages={}, parsed_depth_updates={}",
        raw_messages,
        parsed_depth_updates
    );
}

arb::BookSnapshot fetch_binance_snapshot(
    const arb::Config& config,
    const std::string& symbol
) {
    const int limit = config.market_data.book_snapshot_limit;

    const std::string target =
        "/api/v3/depth?symbol=" + symbol +
        "&limit=" + std::to_string(limit);

    spdlog::info("Fetching Binance depth snapshot: {}", target);

    const auto endpoint = arb::make_https_endpoint(
        config.binance.rest_base_url,
        target
    );

    const arb::HttpsClient client;
    const arb::BinanceSnapshotParser parser;

    const std::string body = client.get(endpoint);

    return parser.parse(body, symbol);
}

arb::BookSnapshot fetch_mexc_snapshot(
    const arb::Config& config,
    const std::string& symbol
) {
    const int limit = config.market_data.book_snapshot_limit;

    const std::string target =
        "/api/v3/depth?symbol=" + symbol +
        "&limit=" + std::to_string(limit);

    spdlog::info("Fetching MEXC depth snapshot: {}", target);

    const auto endpoint = arb::make_https_endpoint(
        config.mexc.rest_base_url,
        target
    );

    const arb::HttpsClient client;
    const arb::MexcSnapshotParser parser;

    const std::string body = client.get(endpoint);

    return parser.parse(body, symbol);
}

arb::BookSnapshot fetch_binance_snapshot_with_retries(
    const arb::Config& config,
    const std::string& symbol,
    int max_attempts = 3
) {
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        try {
            return fetch_binance_snapshot(config, symbol);
        } catch (const std::exception& e) {
            if (attempt == max_attempts) {
                throw;
            }

            spdlog::warn(
                "Binance snapshot failed for {} attempt {}/{}. Retrying. error={}",
                symbol,
                attempt,
                max_attempts,
                e.what()
            );

            std::this_thread::sleep_for(
                std::chrono::milliseconds{250 * attempt}
            );
        }
    }

    throw std::runtime_error("Unreachable Binance snapshot retry state");
}

arb::BookSnapshot fetch_mexc_snapshot_with_retries(
    const arb::Config& config,
    const std::string& symbol,
    int max_attempts = 3
) {
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        try {
            return fetch_mexc_snapshot(config, symbol);
        } catch (const std::exception& e) {
            if (attempt == max_attempts) {
                throw;
            }

            spdlog::warn(
                "MEXC snapshot failed for {} attempt {}/{}. Retrying. error={}",
                symbol,
                attempt,
                max_attempts,
                e.what()
            );

            std::this_thread::sleep_for(
                std::chrono::milliseconds{250 * attempt}
            );
        }
    }

    throw std::runtime_error("Unreachable MEXC snapshot retry state");
}

void run_binance_snapshot(
    const arb::Config& config,
    const std::string& input_symbol
) {
    const std::string symbol = to_upper(input_symbol);

    const auto snapshot = fetch_binance_snapshot(config, symbol);

    arb::OrderBook book;

    book.apply_snapshot(
        snapshot.bids,
        snapshot.asks,
        snapshot.last_update_id
    );

    spdlog::info(
        "Snapshot loaded: symbol={} lastUpdateId={} bids={} asks={}",
        snapshot.symbol,
        snapshot.last_update_id,
        snapshot.bids.size(),
        snapshot.asks.size()
    );

    const auto best_bid = book.best_bid();
    const auto best_ask = book.best_ask();

    if (best_bid.has_value()) {
        spdlog::info(
            "Best bid: price={} quantity={}",
            best_bid->price,
            best_bid->quantity
        );
    }

    if (best_ask.has_value()) {
        spdlog::info(
            "Best ask: price={} quantity={}",
            best_ask->price,
            best_ask->quantity
        );
    }
}

void run_mexc_snapshot(
    const arb::Config& config,
    const std::string& input_symbol
) {
    const std::string symbol = to_upper(input_symbol);

    const auto snapshot = fetch_mexc_snapshot(config, symbol);

    arb::OrderBook book;

    book.apply_snapshot(
        snapshot.bids,
        snapshot.asks,
        snapshot.last_update_id
    );

    spdlog::info(
        "MEXC snapshot loaded: symbol={} lastUpdateId={} bids={} asks={}",
        snapshot.symbol,
        snapshot.last_update_id,
        snapshot.bids.size(),
        snapshot.asks.size()
    );

    const auto best_bid = book.best_bid();
    const auto best_ask = book.best_ask();

    if (best_bid.has_value()) {
        spdlog::info(
            "MEXC best bid: price={} quantity={}",
            best_bid->price,
            best_bid->quantity
        );
    }

    if (best_ask.has_value()) {
        spdlog::info(
            "MEXC best ask: price={} quantity={}",
            best_ask->price,
            best_ask->quantity
        );
    }
}

void print_market_data_store_summary(
    const arb::MarketDataStore& store
) {
    const auto binance_ready_symbols =
        store.ready_symbols(arb::Exchange::Binance);

    const auto mexc_ready_symbols =
        store.ready_symbols(arb::Exchange::Mexc);

    spdlog::info(
        "MarketDataStore summary: binance_books={} binance_ready={} mexc_books={} mexc_ready={}",
        store.book_count(arb::Exchange::Binance),
        binance_ready_symbols.size(),
        store.book_count(arb::Exchange::Mexc),
        mexc_ready_symbols.size()
    );

    for (const auto& symbol : binance_ready_symbols) {
        spdlog::debug("  Binance ready: {}", symbol);
    }

    for (const auto& symbol : mexc_ready_symbols) {
        spdlog::debug("  MEXC ready: {}", symbol);
    }
}

void run_snapshot_top_spread_scan(
    const arb::Config& config
) {
    spdlog::warn(
        "Running top-of-book cross-exchange spread scan from REST snapshots"
    );

    arb::MarketDataStore store;

    std::size_t binance_loaded = 0;
    std::size_t mexc_loaded = 0;

    for (const auto& symbol : config.symbols) {
        try {
            const auto snapshot =
                fetch_binance_snapshot_with_retries(config, symbol);

            arb::OrderBook book;

            book.apply_snapshot(
                snapshot.bids,
                snapshot.asks,
                snapshot.last_update_id
            );

            store.set_book(
                arb::Exchange::Binance,
                symbol,
                book
            );

            ++binance_loaded;

            spdlog::info(
                "Loaded Binance snapshot: symbol={} lastUpdateId={} bids={} asks={}",
                symbol,
                snapshot.last_update_id,
                snapshot.bids.size(),
                snapshot.asks.size()
            );
        } catch (const std::exception& e) {
            spdlog::error(
                "Failed to load Binance snapshot for {}: {}",
                symbol,
                e.what()
            );
        }

        try {
            const auto snapshot =
                fetch_mexc_snapshot_with_retries(config, symbol);

            arb::OrderBook book;

            book.apply_snapshot(
                snapshot.bids,
                snapshot.asks,
                snapshot.last_update_id
            );

            store.set_book(
                arb::Exchange::Mexc,
                symbol,
                book
            );

            ++mexc_loaded;

            spdlog::info(
                "Loaded MEXC snapshot: symbol={} lastUpdateId={} bids={} asks={}",
                symbol,
                snapshot.last_update_id,
                snapshot.bids.size(),
                snapshot.asks.size()
            );
        } catch (const std::exception& e) {
            spdlog::error(
                "Failed to load MEXC snapshot for {}: {}",
                symbol,
                e.what()
            );
        }
    }

    print_market_data_store_summary(store);

    std::vector<TopOfBookSpread> spreads;

    std::size_t ready_pairs = 0;

    for (const auto& symbol : config.symbols) {
        const auto* binance_book = store.get_book(
            arb::Exchange::Binance,
            symbol
        );

        const auto* mexc_book = store.get_book(
            arb::Exchange::Mexc,
            symbol
        );

        if (binance_book == nullptr || mexc_book == nullptr) {
            spdlog::warn(
                "Skipping {} because one of the books is missing",
                symbol
            );

            continue;
        }

        if (
            !binance_book->initialized() ||
            !mexc_book->initialized()
        ) {
            spdlog::warn(
                "Skipping {} because one of the books is not initialized",
                symbol
            );

            continue;
        }

        const auto spread = compute_top_of_book_spread(
            symbol,
            *binance_book,
            *mexc_book,
            config
        );

        if (!spread.has_value()) {
            spdlog::warn(
                "Skipping {} because best bid/ask is missing",
                symbol
            );

            continue;
        }

        ++ready_pairs;
        spreads.push_back(*spread);
    }

    std::sort(
        spreads.begin(),
        spreads.end(),
        [](const auto& left, const auto& right) {
            return left.net_spread_bps > right.net_spread_bps;
        }
    );

    spdlog::info(
        "Top-of-book spread scan complete: symbols={} binance_loaded={} mexc_loaded={} ready_pairs={} spreads={}",
        config.symbols.size(),
        binance_loaded,
        mexc_loaded,
        ready_pairs,
        spreads.size()
    );

    for (const auto& spread : spreads) {
        print_top_of_book_spread(spread);
    }
}

void run_snapshot_arbitrage_scan(
    const arb::Config& config
) {
    spdlog::warn(
        "Running cross-exchange arbitrage scan from REST snapshots"
    );

    arb::MarketDataStore store;

    std::size_t binance_loaded = 0;
    std::size_t mexc_loaded = 0;

    for (const auto& symbol : config.symbols) {
        try {
            const auto snapshot = fetch_binance_snapshot_with_retries(config, symbol);

            arb::OrderBook book;

            book.apply_snapshot(
                snapshot.bids,
                snapshot.asks,
                snapshot.last_update_id
            );

            store.set_book(
                arb::Exchange::Binance,
                symbol,
                book
            );

            ++binance_loaded;

            spdlog::info(
                "Loaded Binance snapshot: symbol={} lastUpdateId={} bids={} asks={}",
                symbol,
                snapshot.last_update_id,
                snapshot.bids.size(),
                snapshot.asks.size()
            );
        } catch (const std::exception& e) {
            spdlog::error(
                "Failed to load Binance snapshot for {}: {}",
                symbol,
                e.what()
            );
        }

        try {
            const auto snapshot = fetch_mexc_snapshot_with_retries(config, symbol);

            arb::OrderBook book;

            book.apply_snapshot(
                snapshot.bids,
                snapshot.asks,
                snapshot.last_update_id
            );

            store.set_book(
                arb::Exchange::Mexc,
                symbol,
                book
            );

            ++mexc_loaded;

            spdlog::info(
                "Loaded MEXC snapshot: symbol={} lastUpdateId={} bids={} asks={}",
                symbol,
                snapshot.last_update_id,
                snapshot.bids.size(),
                snapshot.asks.size()
            );
        } catch (const std::exception& e) {
            spdlog::error(
                "Failed to load MEXC snapshot for {}: {}",
                symbol,
                e.what()
            );
        }
    }

    print_market_data_store_summary(store);

    const arb::ArbitrageEngine engine{
        make_arbitrage_settings(config)
    };

    std::vector<arb::ArbitrageOpportunity> all_opportunities;

    std::size_t ready_pairs = 0;

    for (const auto& symbol : config.symbols) {
        if (
            !store.has_ready_book(arb::Exchange::Binance, symbol) ||
            !store.has_ready_book(arb::Exchange::Mexc, symbol)
        ) {
            spdlog::warn(
                "Skipping {} because one of the books is not ready",
                symbol
            );

            continue;
        }

        const auto* binance_book = store.get_book(
            arb::Exchange::Binance,
            symbol
        );

        const auto* mexc_book = store.get_book(
            arb::Exchange::Mexc,
            symbol
        );

        if (binance_book == nullptr || mexc_book == nullptr) {
            continue;
        }

        ++ready_pairs;

        auto opportunities = engine.scan_symbol(
            symbol,
            *binance_book,
            *mexc_book
        );

        all_opportunities.insert(
            all_opportunities.end(),
            opportunities.begin(),
            opportunities.end()
        );
    }

    std::sort(
        all_opportunities.begin(),
        all_opportunities.end(),
        [](const auto& left, const auto& right) {
            return left.net_spread_bps > right.net_spread_bps;
        }
    );

    spdlog::info(
        "Snapshot arbitrage scan complete: symbols={} binance_loaded={} mexc_loaded={} ready_pairs={} opportunities={}",
        config.symbols.size(),
        binance_loaded,
        mexc_loaded,
        ready_pairs,
        all_opportunities.size()
    );

    if (all_opportunities.empty()) {
        spdlog::info(
            "No opportunities passed filters: min_net_spread_bps={} min_notional={} max_notional={} binance_fee_bps={} mexc_fee_bps={}",
            config.arbitrage.min_net_spread_bps,
            config.arbitrage.min_notional_usdt,
            config.arbitrage.max_notional_usdt,
            config.binance_fees.taker_bps,
            config.mexc_fees.taker_bps
        );

        return;
    }

    for (const auto& opportunity : all_opportunities) {
        print_arbitrage_opportunity(opportunity);
    }
}

void print_local_book_top(
    const arb::BinanceLocalBookManager& manager
) {
    const auto& book = manager.book();

    const auto best_bid = book.best_bid();
    const auto best_ask = book.best_ask();

    if (!best_bid.has_value() || !best_ask.has_value()) {
        spdlog::warn("Local book is ready but best bid/ask is missing");
        return;
    }

    const auto stats = manager.stats();

    spdlog::info(
        "[local-book] symbol={} lastUpdateId={} best_bid={}@{} best_ask={}@{} applied={} stale={} gaps={}",
        manager.symbol(),
        book.last_update_id(),
        best_bid->quantity,
        best_bid->price,
        best_ask->quantity,
        best_ask->price,
        stats.applied_updates,
        stats.ignored_stale_updates,
        stats.gaps_detected
    );
}

void print_mexc_local_book_top(
    const arb::MexcLocalBookManager& manager
) {
    const auto& book = manager.book();

    const auto best_bid = book.best_bid();
    const auto best_ask = book.best_ask();

    if (!best_bid.has_value() || !best_ask.has_value()) {
        spdlog::warn("MEXC local book is ready but best bid/ask is missing");
        return;
    }

    const auto stats = manager.stats();

    spdlog::info(
        "[mexc-local-book] symbol={} lastUpdateId={} best_bid={}@{} best_ask={}@{} applied={} stale={} gaps={}",
        manager.symbol(),
        book.last_update_id(),
        best_bid->quantity,
        best_bid->price,
        best_ask->quantity,
        best_ask->price,
        stats.applied_updates,
        stats.ignored_stale_updates,
        stats.gaps_detected
    );
}

void print_all_local_books(
    const std::unordered_map<std::string, arb::BinanceLocalBookManager>& managers
) {
    for (const auto& [symbol, manager] : managers) {
        if (!manager.ready()) {
            continue;
        }

        const auto& book = manager.book();

        const auto best_bid = book.best_bid();
        const auto best_ask = book.best_ask();

        if (!best_bid.has_value() || !best_ask.has_value()) {
            continue;
        }

        const auto stats = manager.stats();

        spdlog::info(
            "[binance-book] symbol={} lastUpdateId={} bid={}@{} ask={}@{} applied={} stale={} gaps={}",
            symbol,
            book.last_update_id(),
            best_bid->quantity,
            best_bid->price,
            best_ask->quantity,
            best_ask->price,
            stats.applied_updates,
            stats.ignored_stale_updates,
            stats.gaps_detected
        );
    }
}

void print_all_mexc_local_books(
    const std::unordered_map<std::string, arb::MexcLocalBookManager>& managers
) {
    for (const auto& [symbol, manager] : managers) {
        if (!manager.ready()) {
            continue;
        }

        const auto& book = manager.book();

        const auto best_bid = book.best_bid();
        const auto best_ask = book.best_ask();

        if (!best_bid.has_value() || !best_ask.has_value()) {
            continue;
        }

        const auto stats = manager.stats();

        spdlog::info(
            "[mexc-book] symbol={} lastUpdateId={} bid={}@{} ask={}@{} applied={} stale={} gaps={}",
            symbol,
            book.last_update_id(),
            best_bid->quantity,
            best_bid->price,
            best_ask->quantity,
            best_ask->price,
            stats.applied_updates,
            stats.ignored_stale_updates,
            stats.gaps_detected
        );
    }
}

arb::MarketDataStore build_live_store_from_managers(
    const std::unordered_map<std::string, arb::BinanceLocalBookManager>& binance_managers,
    const std::unordered_map<std::string, arb::MexcLocalBookManager>& mexc_managers
) {
    arb::MarketDataStore store;

    for (const auto& [symbol, manager] : binance_managers) {
        if (!manager.ready()) {
            continue;
        }

        store.set_book(
            arb::Exchange::Binance,
            symbol,
            manager.book()
        );
    }

    for (const auto& [symbol, manager] : mexc_managers) {
        if (!manager.ready()) {
            continue;
        }

        store.set_book(
            arb::Exchange::Mexc,
            symbol,
            manager.book()
        );
    }

    return store;
}

arb::MarketDataStore build_store_from_binance_managers(
    const std::unordered_map<std::string, arb::BinanceLocalBookManager>& managers
) {
    arb::MarketDataStore store;

    for (const auto& [symbol, manager] : managers) {
        if (!manager.ready()) {
            continue;
        }

        store.set_book(
            arb::Exchange::Binance,
            symbol,
            manager.book()
        );
    }

    return store;
}

arb::MarketDataStore build_store_from_mexc_managers(
    const std::unordered_map<std::string, arb::MexcLocalBookManager>& managers
) {
    arb::MarketDataStore store;

    for (const auto& [symbol, manager] : managers) {
        if (!manager.ready()) {
            continue;
        }

        store.set_book(
            arb::Exchange::Mexc,
            symbol,
            manager.book()
        );
    }

    return store;
}

void run_binance_local_live(
    const arb::Config& config,
    const std::string& input_symbol,
    std::size_t max_messages
) {
    const std::string symbol = to_upper(input_symbol);

    spdlog::warn(
        "Starting Binance local live book for {}. Press Ctrl+C to stop.",
        symbol
    );

    const std::string stream =
        arb::SubscriptionPlanner::build_stream_name(
            arb::Exchange::Binance,
            symbol
        );

    const std::string target =
        arb::SubscriptionPlanner::build_binance_combined_target({stream});

    spdlog::info("Binance local-live target: {}", target);

    const auto endpoint = arb::make_ws_endpoint(
        config.binance.ws_base_url,
        target
    );

    arb::BinanceLocalBookManager manager{symbol};
    arb::BinanceDepthParser parser;
    arb::WebSocketClient client;

    std::mutex mutex;

    std::atomic<bool> ws_started{false};
    std::atomic<bool> ws_finished{false};

    std::exception_ptr ws_exception = nullptr;

    std::size_t raw_messages = 0;
    std::size_t parsed_updates = 0;
    std::size_t applied_updates = 0;
    std::size_t buffered_updates = 0;

    bool gap_logged = false;

    std::thread ws_thread{
        [&]() {
            try {
                ws_started = true;

                client.run(
                    endpoint,
                    [&](std::string_view message) {
                        const auto update = parser.parse(message);

                        std::lock_guard<std::mutex> lock{mutex};

                        ++raw_messages;

                        if (!update.has_value()) {
                            return;
                        }

                        ++parsed_updates;

                        const auto result = manager.on_update(*update);

                        switch (result) {
                            case arb::LocalBookUpdateResult::Buffered:
                                ++buffered_updates;
                                break;

                            case arb::LocalBookUpdateResult::Applied:
                                ++applied_updates;

                                if (applied_updates % 25 == 0) {
                                    print_local_book_top(manager);
                                }

                                break;

                            case arb::LocalBookUpdateResult::IgnoredStale:
                                break;

                            case arb::LocalBookUpdateResult::GapDetected:
                                if (!gap_logged) {
                                    spdlog::error(
                                        "Gap detected for {}. Local book needs resync.",
                                        symbol
                                    );
                                    gap_logged = true;
                                }
                                break;

                            case arb::LocalBookUpdateResult::SnapshotApplied:
                            case arb::LocalBookUpdateResult::SnapshotRejected:
                            case arb::LocalBookUpdateResult::NotForSymbol:
                                break;
                        }
                    },
                    max_messages
                );
            } catch (...) {
                ws_exception = std::current_exception();
            }

            ws_finished = true;
        }
    };

    while (!ws_started) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    auto wait_for_buffered_updates = [&]() -> bool {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{5};

        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock{mutex};

                if (manager.buffered_updates() > 0) {
                    spdlog::info(
                        "Initial WS buffer collected: buffered_updates={} raw_messages={} parsed_updates={}",
                        manager.buffered_updates(),
                        raw_messages,
                        parsed_updates
                    );

                    return true;
                }
            }

            if (ws_finished) {
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }

        return false;
    };

    bool synced = false;

    constexpr int max_sync_attempts = 3;

    for (int attempt = 1; attempt <= max_sync_attempts; ++attempt) {
        spdlog::info("Local book sync attempt {}/{}", attempt, max_sync_attempts);

        {
            std::lock_guard<std::mutex> lock{mutex};

            if (attempt > 1 || manager.need_resync()) {
                manager.reset();
                gap_logged = false;
            }
        }

        if (!wait_for_buffered_updates()) {
            spdlog::error(
                "No WebSocket updates buffered for {} before snapshot attempt {}",
                symbol,
                attempt
            );
            continue;
        }

        const auto snapshot = fetch_binance_snapshot(config, symbol);

        {
            std::lock_guard<std::mutex> lock{mutex};

            const auto snapshot_result = manager.apply_snapshot(snapshot);

            if (snapshot_result == arb::LocalBookUpdateResult::SnapshotApplied) {
                synced = true;

                spdlog::info(
                    "Snapshot applied: symbol={} lastUpdateId={} buffered_after_apply={}",
                    symbol,
                    snapshot.last_update_id,
                    manager.buffered_updates()
                );

                if (manager.ready()) {
                    print_local_book_top(manager);
                }

                break;
            }

            const auto stats = manager.stats();

            spdlog::error(
                "Failed to apply snapshot for {}. result={} snapshot_lastUpdateId={} buffered={} raw_messages={} parsed_updates={} gaps={}",
                symbol,
                static_cast<int>(snapshot_result),
                snapshot.last_update_id,
                manager.buffered_updates(),
                raw_messages,
                parsed_updates,
                stats.gaps_detected
            );

            manager.reset();
            gap_logged = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{500});
    }

    if (!synced) {
        spdlog::error(
            "Could not synchronize Binance local book for {} after {} attempts",
            symbol,
            max_sync_attempts
        );
    }

    if (ws_thread.joinable()) {
        ws_thread.join();
    }

    if (ws_exception) {
        std::rethrow_exception(ws_exception);
    }

    {
        std::lock_guard<std::mutex> lock{mutex};

        spdlog::info(
            "Binance local live stopped. raw_messages={} parsed_updates={} buffered_updates={} applied_updates={} ready={} need_resync={}",
            raw_messages,
            parsed_updates,
            buffered_updates,
            applied_updates,
            manager.ready(),
            manager.need_resync()
        );

        if (manager.ready()) {
            print_local_book_top(manager);
        }
    }
}

void run_binance_local_live_all(
    const arb::Config& config,
    const std::vector<arb::SubscriptionGroup>& binance_groups,
    std::size_t max_messages
) {
    if (binance_groups.empty()) {
        spdlog::warn("No Binance subscription groups to run");
        return;
    }

    spdlog::warn(
        "Starting Binance multi-symbol local live books. Press Ctrl+C to stop."
    );

    std::unordered_map<std::string, arb::BinanceLocalBookManager> managers;

    for (const auto& symbol : config.symbols) {
        managers.emplace(symbol, arb::BinanceLocalBookManager{symbol});
    }

    arb::BinanceDepthParser parser;

    std::mutex mutex;

    std::atomic<bool> ws_started{false};
    std::atomic<bool> ws_finished{false};

    std::exception_ptr ws_exception = nullptr;

    std::size_t raw_messages = 0;
    std::size_t parsed_updates = 0;
    std::size_t buffered_updates = 0;
    std::size_t applied_updates = 0;
    std::size_t ignored_updates = 0;
    std::size_t gaps = 0;

    std::vector<std::thread> ws_threads;

    for (const auto& group : binance_groups) {
        const auto endpoint = arb::make_ws_endpoint(
            config.binance.ws_base_url,
            group.websocket_target
        );

        spdlog::info(
            "Starting Binance WS group #{} symbols={} target={}",
            group.connection_index + 1,
            group.symbols.size(),
            group.websocket_target
        );

        ws_threads.emplace_back(
            [&, endpoint, group]() {
                try {
                    ws_started = true;

                    arb::WebSocketClient local_client;

                    local_client.run(
                        endpoint,
                        [&](std::string_view message) {
                            const auto update = parser.parse(message);

                            std::lock_guard<std::mutex> lock{mutex};

                            ++raw_messages;

                            if (!update.has_value()) {
                                return;
                            }

                            ++parsed_updates;

                            auto it = managers.find(update->symbol);

                            if (it == managers.end()) {
                                ++ignored_updates;
                                return;
                            }

                            auto& manager = it->second;

                            const auto result = manager.on_update(*update);

                            switch (result) {
                                case arb::LocalBookUpdateResult::Buffered:
                                    ++buffered_updates;
                                    break;

                                case arb::LocalBookUpdateResult::Applied:
                                    ++applied_updates;

                                    if (applied_updates % 200 == 0) {
                                        print_all_local_books(managers);
                                    }

                                    break;

                                case arb::LocalBookUpdateResult::IgnoredStale:
                                    ++ignored_updates;
                                    break;

                                case arb::LocalBookUpdateResult::GapDetected:
                                    ++gaps;
                                    break;

                                case arb::LocalBookUpdateResult::SnapshotApplied:
                                case arb::LocalBookUpdateResult::SnapshotRejected:
                                case arb::LocalBookUpdateResult::NotForSymbol:
                                    break;
                            }
                        },
                        max_messages
                    );
                } catch (...) {
                    std::lock_guard<std::mutex> lock{mutex};

                    if (!ws_exception) {
                        ws_exception = std::current_exception();
                    }
                }

                ws_finished = true;
            }
        );
    }

    while (!ws_started) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    auto wait_for_symbol_buffer = [&](const std::string& symbol) -> bool {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{10};

        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock{mutex};

                const auto it = managers.find(symbol);

                if (it != managers.end() && it->second.buffered_updates() > 0) {
                    return true;
                }
            }

            if (ws_finished) {
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }

        return false;
    };

    std::size_t synced_books = 0;
    std::size_t failed_books = 0;

    for (const auto& symbol : config.symbols) {
        spdlog::info("Synchronizing Binance local book for {}", symbol);

        if (!wait_for_symbol_buffer(symbol)) {
            spdlog::error(
                "No initial WebSocket buffer for {}. Skipping snapshot sync.",
                symbol
            );

            ++failed_books;
            continue;
        }

        constexpr int max_sync_attempts = 3;

        bool synced = false;

        for (int attempt = 1; attempt <= max_sync_attempts; ++attempt) {
            try {
                const auto snapshot = fetch_binance_snapshot(config, symbol);

                std::lock_guard<std::mutex> lock{mutex};

                auto& manager = managers.at(symbol);

                const auto result = manager.apply_snapshot(snapshot);

                if (result == arb::LocalBookUpdateResult::SnapshotApplied) {
                    synced = true;
                    ++synced_books;

                    spdlog::info(
                        "Synced {} lastUpdateId={} buffered_after_apply={}",
                        symbol,
                        snapshot.last_update_id,
                        manager.buffered_updates()
                    );

                    break;
                }

                spdlog::warn(
                    "Snapshot sync failed for {} attempt {}/{} result={}. Resetting manager.",
                    symbol,
                    attempt,
                    max_sync_attempts,
                    static_cast<int>(result)
                );

                manager.reset();

            } catch (const std::exception& e) {
                spdlog::error(
                    "Snapshot request failed for {} attempt {}/{} error={}",
                    symbol,
                    attempt,
                    max_sync_attempts,
                    e.what()
                );
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{500});
        }

        if (!synced) {
            ++failed_books;
        }
    }

    spdlog::info(
        "Initial Binance sync complete: synced_books={} failed_books={}",
        synced_books,
        failed_books
    );

    {
        std::lock_guard<std::mutex> lock{mutex};
        print_all_local_books(managers);
    }

    for (auto& thread : ws_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    if (ws_exception) {
        std::rethrow_exception(ws_exception);
    }

    {
        std::lock_guard<std::mutex> lock{mutex};
    
        std::size_t ready_books = 0;
        std::size_t need_resync_books = 0;
    
        for (const auto& [symbol, manager] : managers) {
            if (manager.ready()) {
                ++ready_books;
            }
    
            if (manager.need_resync()) {
                ++need_resync_books;
            }
        }
    
        spdlog::info(
            "Binance multi-symbol local live stopped. raw_messages={} parsed_updates={} buffered_updates={} applied_updates={} ignored_updates={} gaps={} ready_books={} need_resync_books={}",
            raw_messages,
            parsed_updates,
            buffered_updates,
            applied_updates,
            ignored_updates,
            gaps,
            ready_books,
            need_resync_books
        );
    
        print_all_local_books(managers);
    
        const auto store = build_store_from_binance_managers(managers);
    
        print_market_data_store_summary(store);
    }
}

void run_mexc_live(
    const arb::Config& config,
    const std::vector<arb::SubscriptionGroup>& mexc_groups,
    std::size_t max_messages
) {
    if (mexc_groups.empty()) {
        spdlog::warn("No MEXC subscription groups to run");
        return;
    }

    const auto& group = mexc_groups.front();

    spdlog::warn(
        "Starting MEXC live protobuf mode for socket #{} only. Press Ctrl+C to stop.",
        group.connection_index + 1
    );

    const auto endpoint = arb::make_ws_endpoint(
        config.mexc.ws_base_url,
        "/ws"
    );

    const std::string subscribe_message =
        build_mexc_subscription_message(group.streams);

    spdlog::info("MEXC subscription message: {}", subscribe_message);

    arb::WebSocketClient client;
    arb::MexcDepthParser parser;

    std::size_t raw_messages = 0;
    std::size_t parsed_depth_updates = 0;
    std::size_t ignored_messages = 0;

    client.run(
        endpoint,
        [&](std::string_view message) {
            ++raw_messages;

            try {
                const auto update = parser.parse(message);

                if (!update.has_value()) {
                    ++ignored_messages;
                    spdlog::debug("Ignored MEXC non-depth or non-protobuf message");
                    return;
                }

                ++parsed_depth_updates;

                spdlog::info(
                    "[mexc] depth update #{} symbol={} fromVersion={} toVersion={} bids={} asks={} event_latency_ms={}",
                    parsed_depth_updates,
                    update->symbol,
                    update->first_update_id,
                    update->final_update_id,
                    update->bids.size(),
                    update->asks.size(),
                    update->local_receive_time_ms - update->exchange_event_time_ms
                );

                if (!update->bids.empty()) {
                    spdlog::debug(
                        "  first bid: price={} quantity={}",
                        update->bids.front().price,
                        update->bids.front().quantity
                    );
                }

                if (!update->asks.empty()) {
                    spdlog::debug(
                        "  first ask: price={} quantity={}",
                        update->asks.front().price,
                        update->asks.front().quantity
                    );
                }

            } catch (const std::exception& e) {
                ++ignored_messages;
                spdlog::error("Failed to parse MEXC message: {}", e.what());
            }
        },
        max_messages,
        {subscribe_message}
    );

    spdlog::info(
        "MEXC live stopped. raw_messages={} parsed_depth_updates={} ignored_messages={}",
        raw_messages,
        parsed_depth_updates,
        ignored_messages
    );
}

void run_mexc_local_live(
    const arb::Config& config,
    const std::string& input_symbol,
    std::size_t max_messages
) {
    const std::string symbol = to_upper(input_symbol);

    spdlog::warn(
        "Starting MEXC local live book for {}. Press Ctrl+C to stop.",
        symbol
    );

    const std::string stream =
        arb::SubscriptionPlanner::build_stream_name(
            arb::Exchange::Mexc,
            symbol
        );

    const std::string subscribe_message =
        build_mexc_subscription_message({stream});

    spdlog::info("MEXC local-live subscription: {}", subscribe_message);

    const auto endpoint = arb::make_ws_endpoint(
        config.mexc.ws_base_url,
        "/ws"
    );

    arb::MexcLocalBookManager manager{symbol};
    arb::MexcDepthParser parser;
    arb::WebSocketClient client;

    std::mutex mutex;

    std::atomic<bool> ws_started{false};
    std::atomic<bool> ws_finished{false};

    std::exception_ptr ws_exception = nullptr;

    std::size_t raw_messages = 0;
    std::size_t parsed_updates = 0;
    std::size_t ignored_messages = 0;
    std::size_t buffered_updates = 0;
    std::size_t applied_updates = 0;

    bool gap_logged = false;

    std::thread ws_thread{
        [&]() {
            try {
                ws_started = true;

                client.run(
                    endpoint,
                    [&](std::string_view message) {
                        const auto update = parser.parse(message);

                        std::lock_guard<std::mutex> lock{mutex};

                        ++raw_messages;

                        if (!update.has_value()) {
                            ++ignored_messages;
                            return;
                        }

                        ++parsed_updates;

                        const auto result = manager.on_update(*update);

                        switch (result) {
                            case arb::MexcLocalBookUpdateResult::Buffered:
                                ++buffered_updates;
                                break;

                            case arb::MexcLocalBookUpdateResult::Applied:
                                ++applied_updates;

                                if (applied_updates % 25 == 0) {
                                    print_mexc_local_book_top(manager);
                                }

                                break;

                            case arb::MexcLocalBookUpdateResult::IgnoredStale:
                                break;

                            case arb::MexcLocalBookUpdateResult::GapDetected:
                                if (!gap_logged) {
                                    spdlog::error(
                                        "MEXC gap detected for {}. Local book needs resync.",
                                        symbol
                                    );

                                    gap_logged = true;
                                }

                                break;

                            case arb::MexcLocalBookUpdateResult::SnapshotApplied:
                            case arb::MexcLocalBookUpdateResult::SnapshotRejected:
                            case arb::MexcLocalBookUpdateResult::NotForSymbol:
                                break;
                        }
                    },
                    max_messages,
                    {subscribe_message}
                );
            } catch (...) {
                ws_exception = std::current_exception();
            }

            ws_finished = true;
        }
    };

    while (!ws_started) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    auto wait_for_buffered_updates = [&]() -> bool {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{10};

        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock{mutex};

                if (manager.buffered_updates() > 0) {
                    spdlog::info(
                        "Initial MEXC WS buffer collected: buffered_updates={} raw_messages={} parsed_updates={} ignored_messages={}",
                        manager.buffered_updates(),
                        raw_messages,
                        parsed_updates,
                        ignored_messages
                    );

                    return true;
                }
            }

            if (ws_finished) {
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }

        return false;
    };

    bool synced = false;

    constexpr int max_sync_attempts = 3;

    for (int attempt = 1; attempt <= max_sync_attempts; ++attempt) {
        spdlog::info(
            "MEXC local book sync attempt {}/{}",
            attempt,
            max_sync_attempts
        );

        {
            std::lock_guard<std::mutex> lock{mutex};

            if (attempt > 1 || manager.need_resync()) {
                manager.reset();
                gap_logged = false;
            }
        }

        if (!wait_for_buffered_updates()) {
            spdlog::error(
                "No MEXC WebSocket updates buffered for {} before snapshot attempt {}",
                symbol,
                attempt
            );

            continue;
        }

        const auto snapshot = fetch_mexc_snapshot(config, symbol);

        {
            std::lock_guard<std::mutex> lock{mutex};

            const auto snapshot_result = manager.apply_snapshot(snapshot);

            if (snapshot_result == arb::MexcLocalBookUpdateResult::SnapshotApplied) {
                synced = true;

                spdlog::info(
                    "MEXC snapshot applied: symbol={} lastUpdateId={} buffered_after_apply={}",
                    symbol,
                    snapshot.last_update_id,
                    manager.buffered_updates()
                );

                if (manager.ready()) {
                    print_mexc_local_book_top(manager);
                }

                break;
            }

            const auto stats = manager.stats();

            spdlog::error(
                "Failed to apply MEXC snapshot for {}. result={} snapshot_lastUpdateId={} buffered={} raw_messages={} parsed_updates={} ignored_messages={} gaps={}",
                symbol,
                static_cast<int>(snapshot_result),
                snapshot.last_update_id,
                manager.buffered_updates(),
                raw_messages,
                parsed_updates,
                ignored_messages,
                stats.gaps_detected
            );

            manager.reset();
            gap_logged = false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{500});
    }

    if (!synced) {
        spdlog::error(
            "Could not synchronize MEXC local book for {} after {} attempts",
            symbol,
            max_sync_attempts
        );
    }

    if (ws_thread.joinable()) {
        ws_thread.join();
    }

    if (ws_exception) {
        std::rethrow_exception(ws_exception);
    }

    {
        std::lock_guard<std::mutex> lock{mutex};

        spdlog::info(
            "MEXC local live stopped. raw_messages={} parsed_updates={} ignored_messages={} buffered_updates={} applied_updates={} ready={} need_resync={}",
            raw_messages,
            parsed_updates,
            ignored_messages,
            buffered_updates,
            applied_updates,
            manager.ready(),
            manager.need_resync()
        );

        if (manager.ready()) {
            print_mexc_local_book_top(manager);
        }
    }
}

void run_mexc_local_live_all(
    const arb::Config& config,
    const std::vector<arb::SubscriptionGroup>& mexc_groups,
    std::size_t max_messages
) {
    if (mexc_groups.empty()) {
        spdlog::warn("No MEXC subscription groups to run");
        return;
    }

    spdlog::warn(
        "Starting MEXC multi-symbol local live books. Press Ctrl+C to stop."
    );

    std::unordered_map<std::string, arb::MexcLocalBookManager> managers;

    for (const auto& symbol : config.symbols) {
        managers.emplace(symbol, arb::MexcLocalBookManager{symbol});
    }

    arb::MexcDepthParser parser;

    std::mutex mutex;

    std::atomic<bool> ws_started{false};
    std::atomic<bool> ws_finished{false};

    std::exception_ptr ws_exception = nullptr;

    std::size_t raw_messages = 0;
    std::size_t parsed_updates = 0;
    std::size_t ignored_messages = 0;
    std::size_t buffered_updates = 0;
    std::size_t applied_updates = 0;
    std::size_t ignored_updates = 0;
    std::size_t gaps = 0;

    std::vector<std::thread> ws_threads;

    for (const auto& group : mexc_groups) {
        const auto endpoint = arb::make_ws_endpoint(
            config.mexc.ws_base_url,
            "/ws"
        );

        const std::string subscribe_message =
            build_mexc_subscription_message(group.streams);

        spdlog::info(
            "Starting MEXC WS group #{} symbols={} subscribe={}",
            group.connection_index + 1,
            group.symbols.size(),
            subscribe_message
        );

        ws_threads.emplace_back(
            [&, endpoint, subscribe_message]() {
                try {
                    ws_started = true;

                    arb::WebSocketClient local_client;

                    local_client.run(
                        endpoint,
                        [&](std::string_view message) {
                            const auto update = parser.parse(message);

                            std::lock_guard<std::mutex> lock{mutex};

                            ++raw_messages;

                            if (!update.has_value()) {
                                ++ignored_messages;
                                return;
                            }

                            ++parsed_updates;

                            auto it = managers.find(update->symbol);

                            if (it == managers.end()) {
                                ++ignored_updates;
                                return;
                            }

                            auto& manager = it->second;

                            const auto result = manager.on_update(*update);

                            switch (result) {
                                case arb::MexcLocalBookUpdateResult::Buffered:
                                    ++buffered_updates;
                                    break;

                                case arb::MexcLocalBookUpdateResult::Applied:
                                    ++applied_updates;

                                    if (applied_updates % 200 == 0) {
                                        print_all_mexc_local_books(managers);
                                    }

                                    break;

                                case arb::MexcLocalBookUpdateResult::IgnoredStale:
                                    ++ignored_updates;
                                    break;

                                case arb::MexcLocalBookUpdateResult::GapDetected:
                                    ++gaps;
                                    break;

                                case arb::MexcLocalBookUpdateResult::SnapshotApplied:
                                case arb::MexcLocalBookUpdateResult::SnapshotRejected:
                                case arb::MexcLocalBookUpdateResult::NotForSymbol:
                                    break;
                            }
                        },
                        max_messages,
                        {subscribe_message}
                    );
                } catch (...) {
                    std::lock_guard<std::mutex> lock{mutex};

                    if (!ws_exception) {
                        ws_exception = std::current_exception();
                    }
                }

                ws_finished = true;
            }
        );
    }

    while (!ws_started) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    auto wait_for_symbol_buffer = [&](const std::string& symbol) -> bool {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{10};

        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock{mutex};

                const auto it = managers.find(symbol);

                if (it != managers.end() && it->second.buffered_updates() > 0) {
                    return true;
                }
            }

            if (ws_finished) {
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }

        return false;
    };

    std::size_t synced_books = 0;
    std::size_t failed_books = 0;

    for (const auto& symbol : config.symbols) {
        spdlog::info("Synchronizing MEXC local book for {}", symbol);

        if (!wait_for_symbol_buffer(symbol)) {
            spdlog::error(
                "No initial MEXC WebSocket buffer for {}. Skipping snapshot sync.",
                symbol
            );

            ++failed_books;
            continue;
        }

        constexpr int max_sync_attempts = 3;

        bool synced = false;

        for (int attempt = 1; attempt <= max_sync_attempts; ++attempt) {
            try {
                const auto snapshot = fetch_mexc_snapshot(config, symbol);

                std::lock_guard<std::mutex> lock{mutex};

                auto& manager = managers.at(symbol);

                const auto result = manager.apply_snapshot(snapshot);

                if (result == arb::MexcLocalBookUpdateResult::SnapshotApplied) {
                    synced = true;
                    ++synced_books;

                    spdlog::info(
                        "Synced MEXC {} lastUpdateId={} buffered_after_apply={}",
                        symbol,
                        snapshot.last_update_id,
                        manager.buffered_updates()
                    );

                    break;
                }

                spdlog::warn(
                    "MEXC snapshot sync failed for {} attempt {}/{} result={}. Resetting manager.",
                    symbol,
                    attempt,
                    max_sync_attempts,
                    static_cast<int>(result)
                );

                manager.reset();

            } catch (const std::exception& e) {
                spdlog::error(
                    "MEXC snapshot request failed for {} attempt {}/{} error={}",
                    symbol,
                    attempt,
                    max_sync_attempts,
                    e.what()
                );
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{500});
        }

        if (!synced) {
            ++failed_books;
        }
    }

    spdlog::info(
        "Initial MEXC sync complete: synced_books={} failed_books={}",
        synced_books,
        failed_books
    );

    {
        std::lock_guard<std::mutex> lock{mutex};
        print_all_mexc_local_books(managers);
    }

    for (auto& thread : ws_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    if (ws_exception) {
        std::rethrow_exception(ws_exception);
    }

    {
        std::lock_guard<std::mutex> lock{mutex};

        std::size_t ready_books = 0;
        std::size_t need_resync_books = 0;

        for (const auto& [symbol, manager] : managers) {
            if (manager.ready()) {
                ++ready_books;
            }

            if (manager.need_resync()) {
                ++need_resync_books;
            }
        }

        spdlog::info(
            "MEXC multi-symbol local live stopped. raw_messages={} parsed_updates={} ignored_messages={} buffered_updates={} applied_updates={} ignored_updates={} gaps={} ready_books={} need_resync_books={}",
            raw_messages,
            parsed_updates,
            ignored_messages,
            buffered_updates,
            applied_updates,
            ignored_updates,
            gaps,
            ready_books,
            need_resync_books
        );

        print_all_mexc_local_books(managers);

        const auto store = build_store_from_mexc_managers(managers);

        print_market_data_store_summary(store);
    }
}

void run_live_arbitrage_scan(
    const arb::Config& config,
    const std::vector<arb::SubscriptionGroup>& binance_groups,
    const std::vector<arb::SubscriptionGroup>& mexc_groups,
    std::size_t max_messages
) {
    if (binance_groups.empty()) {
        spdlog::warn("No Binance subscription groups to run");
        return;
    }

    if (mexc_groups.empty()) {
        spdlog::warn("No MEXC subscription groups to run");
        return;
    }

    std::unordered_map<std::string, arb::BinanceLocalBookManager> binance_managers;
    std::unordered_map<std::string, arb::MexcLocalBookManager> mexc_managers;

    for (const auto& symbol : config.symbols) {
        binance_managers.emplace(
            symbol,
            arb::BinanceLocalBookManager{symbol}
        );

        mexc_managers.emplace(
            symbol,
            arb::MexcLocalBookManager{symbol}
        );
    }

    arb::BinanceDepthParser binance_parser;
    arb::MexcDepthParser mexc_parser;

    const arb::ArbitrageEngine engine{
        make_arbitrage_settings(config)
    };

    std::mutex mutex;

    std::exception_ptr ws_exception = nullptr;

    const std::size_t total_ws_threads =
        binance_groups.size() + mexc_groups.size();

    std::atomic<std::size_t> started_ws_threads{0};
    std::atomic<std::size_t> finished_ws_threads{0};

    std::size_t binance_raw_messages = 0;
    std::size_t binance_parsed_updates = 0;
    std::size_t binance_ignored_messages = 0;

    std::size_t mexc_raw_messages = 0;
    std::size_t mexc_parsed_updates = 0;
    std::size_t mexc_ignored_messages = 0;

    std::size_t binance_gaps = 0;
    std::size_t mexc_gaps = 0;

    std::vector<std::thread> ws_threads;

    for (const auto& group : binance_groups) {
        const auto endpoint = arb::make_ws_endpoint(
            config.binance.ws_base_url,
            group.websocket_target
        );

        spdlog::info(
            "Starting Binance WS group #{} symbols={} target={}",
            group.connection_index + 1,
            group.symbols.size(),
            group.websocket_target
        );

        ws_threads.emplace_back(
            [&, endpoint]() {
                try {
                    ++started_ws_threads;

                    arb::WebSocketClient client;

                    client.run(
                        endpoint,
                        [&](std::string_view message) {
                            const auto update = binance_parser.parse(message);

                            std::lock_guard<std::mutex> lock{mutex};

                            ++binance_raw_messages;

                            if (!update.has_value()) {
                                ++binance_ignored_messages;
                                return;
                            }

                            ++binance_parsed_updates;

                            auto it = binance_managers.find(update->symbol);

                            if (it == binance_managers.end()) {
                                ++binance_ignored_messages;
                                return;
                            }

                            auto& manager = it->second;

                            manager.on_update(*update);

                            if (manager.need_resync()) {
                                ++binance_gaps;
                            }
                        },
                        max_messages,
                        {}
                    );
                } catch (...) {
                    std::lock_guard<std::mutex> lock{mutex};

                    if (!ws_exception) {
                        ws_exception = std::current_exception();
                    }
                }

                ++finished_ws_threads;
            }
        );
    }

    for (const auto& group : mexc_groups) {
        const auto endpoint = arb::make_ws_endpoint(
            config.mexc.ws_base_url,
            "/ws"
        );

        const std::string subscribe_message =
            build_mexc_subscription_message(group.streams);

        spdlog::info(
            "Starting MEXC WS group #{} symbols={} subscribe={}",
            group.connection_index + 1,
            group.symbols.size(),
            subscribe_message
        );

        ws_threads.emplace_back(
            [&, endpoint, subscribe_message]() {
                try {
                    ++started_ws_threads;

                    arb::WebSocketClient client;

                    client.run(
                        endpoint,
                        [&](std::string_view message) {
                            const auto update = mexc_parser.parse(message);

                            std::lock_guard<std::mutex> lock{mutex};

                            ++mexc_raw_messages;

                            if (!update.has_value()) {
                                ++mexc_ignored_messages;
                                return;
                            }

                            ++mexc_parsed_updates;

                            auto it = mexc_managers.find(update->symbol);

                            if (it == mexc_managers.end()) {
                                ++mexc_ignored_messages;
                                return;
                            }

                            auto& manager = it->second;

                            manager.on_update(*update);

                            if (manager.need_resync()) {
                                ++mexc_gaps;
                            }
                        },
                        max_messages,
                        {subscribe_message}
                    );
                } catch (...) {
                    std::lock_guard<std::mutex> lock{mutex};

                    if (!ws_exception) {
                        ws_exception = std::current_exception();
                    }
                }

                ++finished_ws_threads;
            }
        );
    }

    while (started_ws_threads.load() < total_ws_threads) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    auto wait_for_binance_buffer = [&](const std::string& symbol) -> bool {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{15};

        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock{mutex};

                const auto it = binance_managers.find(symbol);

                if (
                    it != binance_managers.end() &&
                    it->second.buffered_updates() > 0
                ) {
                    return true;
                }
            }

            if (finished_ws_threads.load() == total_ws_threads) {
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }

        return false;
    };

    auto wait_for_mexc_buffer = [&](const std::string& symbol) -> bool {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{15};

        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock{mutex};

                const auto it = mexc_managers.find(symbol);

                if (
                    it != mexc_managers.end() &&
                    it->second.buffered_updates() > 0
                ) {
                    return true;
                }
            }

            if (finished_ws_threads.load() == total_ws_threads) {
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }

        return false;
    };

    auto sync_binance_symbol = [&](const std::string& symbol) -> bool {
        constexpr int max_sync_attempts = 3;

        for (int attempt = 1; attempt <= max_sync_attempts; ++attempt) {
            spdlog::info(
                "Synchronizing Binance local book for {} attempt {}/{}",
                symbol,
                attempt,
                max_sync_attempts
            );

            {
                std::lock_guard<std::mutex> lock{mutex};

                if (attempt > 1) {
                    binance_managers.at(symbol).reset();
                }
            }

            if (!wait_for_binance_buffer(symbol)) {
                spdlog::error(
                    "No initial Binance WebSocket buffer for {}",
                    symbol
                );

                continue;
            }

            try {
                const auto snapshot =
                    fetch_binance_snapshot_with_retries(config, symbol);

                std::lock_guard<std::mutex> lock{mutex};

                auto& manager = binance_managers.at(symbol);

                manager.apply_snapshot(snapshot);

                if (manager.ready()) {
                    spdlog::info(
                        "Synced Binance {} lastUpdateId={} buffered_after_apply={}",
                        symbol,
                        snapshot.last_update_id,
                        manager.buffered_updates()
                    );

                    return true;
                }

                spdlog::warn(
                    "Binance snapshot did not synchronize {}. need_resync={} buffered={}",
                    symbol,
                    manager.need_resync(),
                    manager.buffered_updates()
                );

            } catch (const std::exception& e) {
                spdlog::error(
                    "Failed to sync Binance {} attempt {}/{} error={}",
                    symbol,
                    attempt,
                    max_sync_attempts,
                    e.what()
                );
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{500});
        }

        return false;
    };

    auto sync_mexc_symbol = [&](const std::string& symbol) -> bool {
        constexpr int max_sync_attempts = 3;

        for (int attempt = 1; attempt <= max_sync_attempts; ++attempt) {
            spdlog::info(
                "Synchronizing MEXC local book for {} attempt {}/{}",
                symbol,
                attempt,
                max_sync_attempts
            );

            {
                std::lock_guard<std::mutex> lock{mutex};

                if (attempt > 1) {
                    mexc_managers.at(symbol).reset();
                }
            }

            if (!wait_for_mexc_buffer(symbol)) {
                spdlog::error(
                    "No initial MEXC WebSocket buffer for {}",
                    symbol
                );

                continue;
            }

            try {
                const auto snapshot =
                    fetch_mexc_snapshot_with_retries(config, symbol);

                std::lock_guard<std::mutex> lock{mutex};

                auto& manager = mexc_managers.at(symbol);

                manager.apply_snapshot(snapshot);

                if (manager.ready()) {
                    spdlog::info(
                        "Synced MEXC {} lastUpdateId={} buffered_after_apply={}",
                        symbol,
                        snapshot.last_update_id,
                        manager.buffered_updates()
                    );

                    return true;
                }

                spdlog::warn(
                    "MEXC snapshot did not synchronize {}. need_resync={} buffered={}",
                    symbol,
                    manager.need_resync(),
                    manager.buffered_updates()
                );

            } catch (const std::exception& e) {
                spdlog::error(
                    "Failed to sync MEXC {} attempt {}/{} error={}",
                    symbol,
                    attempt,
                    max_sync_attempts,
                    e.what()
                );
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{500});
        }

        return false;
    };

    std::size_t binance_synced = 0;
    std::size_t mexc_synced = 0;

    for (const auto& symbol : config.symbols) {
        if (sync_binance_symbol(symbol)) {
            ++binance_synced;
        }
    }
    
    for (const auto& symbol : config.symbols) {
        if (sync_mexc_symbol(symbol)) {
            ++mexc_synced;
        }
    }

    spdlog::info(
        "Initial live sync complete: binance_synced={} mexc_synced={}",
        binance_synced,
        mexc_synced
    );

    {
        std::lock_guard<std::mutex> lock{mutex};

        const auto store = build_live_store_from_managers(
            binance_managers,
            mexc_managers
        );

        print_market_data_store_summary(store);
    }

    std::size_t scan_index = 0;

    while (finished_ws_threads.load() < total_ws_threads) {
        std::this_thread::sleep_for(std::chrono::seconds{2});

        arb::MarketDataStore store;
    
        {
            std::lock_guard<std::mutex> lock{mutex};
    
            store = build_live_store_from_managers(
                binance_managers,
                mexc_managers
            );
        }
    
        ++scan_index;
    
        scan_live_store_once(
            config,
            store,
            engine,
            scan_index
        );
    }

    for (auto& thread : ws_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    if (ws_exception) {
        std::rethrow_exception(ws_exception);
    }

    {
        std::lock_guard<std::mutex> lock{mutex};

        std::size_t binance_ready = 0;
        std::size_t mexc_ready = 0;
        std::size_t binance_need_resync = 0;
        std::size_t mexc_need_resync = 0;

        for (const auto& [symbol, manager] : binance_managers) {
            if (manager.ready()) {
                ++binance_ready;
            }

            if (manager.need_resync()) {
                ++binance_need_resync;
            }
        }

        for (const auto& [symbol, manager] : mexc_managers) {
            if (manager.ready()) {
                ++mexc_ready;
            }

            if (manager.need_resync()) {
                ++mexc_need_resync;
            }
        }

        spdlog::info(
            "Live scanner stopped. binance_raw={} binance_parsed={} binance_ignored={} mexc_raw={} mexc_parsed={} mexc_ignored={} binance_ready={} mexc_ready={} binance_need_resync={} mexc_need_resync={} binance_gaps={} mexc_gaps={}",
            binance_raw_messages,
            binance_parsed_updates,
            binance_ignored_messages,
            mexc_raw_messages,
            mexc_parsed_updates,
            mexc_ignored_messages,
            binance_ready,
            mexc_ready,
            binance_need_resync,
            mexc_need_resync,
            binance_gaps,
            mexc_gaps
        );

        const auto final_store = build_live_store_from_managers(
            binance_managers,
            mexc_managers
        );

        print_market_data_store_summary(final_store);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string config_path = get_config_path(argc, argv);
    const bool binance_live = has_arg(argc, argv, "--binance-live");
    const bool mexc_live = has_arg(argc, argv, "--mexc-live");
    const std::size_t max_messages = get_max_messages_arg(argc, argv);
    const auto binance_snapshot_symbol = get_arg_value(argc, argv, "--binance-snapshot=");
    const auto binance_local_live_symbol = get_arg_value(argc, argv, "--binance-local-live=");
    const bool binance_local_live_all = has_arg(argc, argv, "--binance-local-live-all");
    const auto mexc_snapshot_symbol = get_arg_value(argc, argv, "--mexc-snapshot=");
    const auto mexc_local_live_symbol = get_arg_value(argc, argv, "--mexc-local-live=");
    const bool mexc_local_live_all = has_arg(argc, argv, "--mexc-local-live-all");
    const bool scan_snapshots = has_arg(argc, argv, "--scan-snapshots");
    const bool scan_snapshots_top = has_arg(argc, argv, "--scan-snapshots-top");
    const bool scan_live = has_arg(argc, argv, "--scan-live");

    try {
        const arb::Config config = arb::load_config(config_path);
        configure_logger(config.log_level);

        spdlog::info("Loaded config: {}", config_path);
        spdlog::info("Symbols: {}", config.symbols.size());

        arb::SubscriptionPlanner planner;

        std::vector<arb::SubscriptionGroup> binance_groups;

        if (config.binance.enabled) {
            binance_groups = planner.plan(
                arb::Exchange::Binance,
                config.symbols,
                config.binance.max_streams_per_connection
            );

            print_groups("Binance", binance_groups);
        } else {
            spdlog::warn("Binance is disabled");
        }

        std::vector<arb::SubscriptionGroup> mexc_groups;

        if (config.mexc.enabled) {
            mexc_groups = planner.plan(
                arb::Exchange::Mexc,
                config.symbols,
                config.mexc.max_streams_per_connection
            );

            print_groups("MEXC", mexc_groups);
        } else {
            spdlog::warn("MEXC is disabled");
        }

        if (binance_snapshot_symbol.has_value()) {
            run_binance_snapshot(config, *binance_snapshot_symbol);
        } else if (mexc_snapshot_symbol.has_value()) {
            run_mexc_snapshot(config, *mexc_snapshot_symbol);
        } else if (scan_snapshots) {
            run_snapshot_arbitrage_scan(config);
        } else if (scan_snapshots_top) {
            run_snapshot_top_spread_scan(config);
        } else if (scan_live) {
            run_live_arbitrage_scan(
                config,
                binance_groups,
                mexc_groups,
                max_messages
            );
        } else if (binance_local_live_symbol.has_value()) {
            run_binance_local_live(
                config,
                *binance_local_live_symbol,
                max_messages
            );
        } else if (mexc_local_live_symbol.has_value()) {
            run_mexc_local_live(
                config,
                *mexc_local_live_symbol,
                max_messages
            );
        } else if (binance_local_live_all) {
            run_binance_local_live_all(
                config,
                binance_groups,
                max_messages
            );
        } else if (mexc_local_live_all) {
            run_mexc_local_live_all(
                config,
                mexc_groups,
                max_messages
            );
        } else if (binance_live) {
            run_binance_live(config, binance_groups, max_messages);
        } else if (mexc_live) {
            run_mexc_live(config, mexc_groups, max_messages);
        } else {
            spdlog::info("Foundation check completed successfully");
            spdlog::info(
                "To run Binance live mode: ./build/arbitrage_engine config/config.yaml --binance-live --max-messages=20"
            );
            spdlog::info(
                "To fetch Binance snapshot: ./build/arbitrage_engine config/config.yaml --binance-snapshot=BTCUSDT"
            );
            spdlog::info(
                "To run Binance local live book: ./build/arbitrage_engine config/config.yaml --binance-local-live=BTCUSDT --max-messages=300"
            );
            spdlog::info(
                "To run Binance all-symbol local live books: ./build/arbitrage_engine config/config.yaml --binance-local-live-all --max-messages=3000"
            );
            spdlog::info(
                "To fetch MEXC snapshot: ./build/arbitrage_engine config/config.yaml --mexc-snapshot=BTCUSDT"
            );
            spdlog::info(
                "To run MEXC live mode: ./build/arbitrage_engine config/config.yaml --mexc-live --max-messages=20"
            );
            spdlog::info(
                "To run MEXC local live book: ./build/arbitrage_engine config/config.yaml --mexc-local-live=BTCUSDT --max-messages=1000"
            );
            spdlog::info(
                "To run MEXC all-symbol local live books: ./build/arbitrage_engine config/config.yaml --mexc-local-live-all --max-messages=3000"
            );
            spdlog::info(
                "To scan arbitrage from REST snapshots: ./build/arbitrage_engine config/config.yaml --scan-snapshots"
            );
            spdlog::info(
                "To scan top-of-book spreads from REST snapshots: ./build/arbitrage_engine config/config.yaml --scan-snapshots-top"
            );
            spdlog::info(
                "To run live arbitrage scanner: ./build/arbitrage_engine config/config.yaml --scan-live --max-messages=10000"
            );
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return 1;
    }
}