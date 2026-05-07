#include "MexcSnapshotParser.hpp"

#include "../core/Exchange.hpp"
#include "../util/Time.hpp"

#include <nlohmann/json.hpp>

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

BookSnapshot MexcSnapshotParser::parse(
    std::string_view body,
    const std::string& symbol
) const {
    const auto root = nlohmann::json::parse(body);

    if (!root.contains("lastUpdateId")) {
        throw std::runtime_error("MEXC snapshot missing lastUpdateId");
    }

    if (!root.contains("bids") || !root.contains("asks")) {
        throw std::runtime_error("MEXC snapshot missing bids/asks");
    }

    return BookSnapshot{
        .exchange = Exchange::Mexc,
        .symbol = symbol,
        .bids = parse_levels(root.at("bids")),
        .asks = parse_levels(root.at("asks")),
        .last_update_id = root.at("lastUpdateId").get<std::uint64_t>(),
        .local_receive_time_ms = now_ms()
    };
}

}  // namespace arb