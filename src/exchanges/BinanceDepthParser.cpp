#include "BinanceDepthParser.hpp"

#include "../core/Exchange.hpp"
#include "../util/Time.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace arb {
namespace {

double parse_double_from_json_string(const nlohmann::json& value) {
    if (value.is_string()) {
        return std::stod(value.get<std::string>());
    }

    if (value.is_number()) {
        return value.get<double>();
    }

    throw std::runtime_error("Expected price/quantity as string or number");
}

std::vector<PriceLevel> parse_levels(const nlohmann::json& levels) {
    std::vector<PriceLevel> result;
    result.reserve(levels.size());

    for (const auto& level : levels) {
        if (!level.is_array() || level.size() < 2) {
            continue;
        }

        result.push_back(
            PriceLevel{
                .price = parse_double_from_json_string(level[0]),
                .quantity = parse_double_from_json_string(level[1])
            }
        );
    }

    return result;
}

}  // namespace

std::optional<BookUpdate> BinanceDepthParser::parse(
    std::string_view message
) const {
    const auto root = nlohmann::json::parse(message);

    const nlohmann::json* data = &root;

    // Combined stream:
    // {
    //   "stream": "btcusdt@depth@100ms",
    //   "data": {...}
    // }
    if (root.contains("data")) {
        data = &root.at("data");
    }

    if (!data->contains("e") || data->at("e") != "depthUpdate") {
        return std::nullopt;
    }

    BookUpdate update{
        .exchange = Exchange::Binance,
        .symbol = data->at("s").get<std::string>(),
        .bids = parse_levels(data->at("b")),
        .asks = parse_levels(data->at("a")),
        .first_update_id = data->at("U").get<std::uint64_t>(),
        .final_update_id = data->at("u").get<std::uint64_t>(),
        .exchange_event_time_ms = data->at("E").get<std::int64_t>(),
        .local_receive_time_ms = now_ms()
    };

    return update;
}

}  // namespace arb