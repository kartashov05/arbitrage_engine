#include "MexcDepthParser.hpp"

#include "../core/Exchange.hpp"
#include "../util/Time.hpp"

#include "PushDataV3ApiWrapper.pb.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace arb {
namespace {

double parse_double_from_string(const std::string& value) {
    return std::stod(value);
}

std::uint64_t parse_uint64_from_string(const std::string& value) {
    return static_cast<std::uint64_t>(std::stoull(value));
}

}  // namespace

std::optional<BookUpdate> MexcDepthParser::parse(
    std::string_view message
) const {
    PushDataV3ApiWrapper wrapper;

    if (!wrapper.ParseFromArray(message.data(), static_cast<int>(message.size()))) {
        return std::nullopt;
    }

    if (!wrapper.has_publicaggredepths()) {
        return std::nullopt;
    }

    const auto& depths = wrapper.publicaggredepths();

    std::vector<PriceLevel> bids;
    bids.reserve(static_cast<std::size_t>(depths.bids_size()));

    for (const auto& bid : depths.bids()) {
        bids.push_back(
            PriceLevel{
                .price = parse_double_from_string(bid.price()),
                .quantity = parse_double_from_string(bid.quantity())
            }
        );
    }

    std::vector<PriceLevel> asks;
    asks.reserve(static_cast<std::size_t>(depths.asks_size()));

    for (const auto& ask : depths.asks()) {
        asks.push_back(
            PriceLevel{
                .price = parse_double_from_string(ask.price()),
                .quantity = parse_double_from_string(ask.quantity())
            }
        );
    }

    const std::int64_t event_time =
        wrapper.has_sendtime()
            ? wrapper.sendtime()
            : now_ms();

    return BookUpdate{
        .exchange = Exchange::Mexc,
        .symbol = wrapper.symbol(),
        .bids = std::move(bids),
        .asks = std::move(asks),
        .first_update_id = parse_uint64_from_string(depths.fromversion()),
        .final_update_id = parse_uint64_from_string(depths.toversion()),
        .exchange_event_time_ms = event_time,
        .local_receive_time_ms = now_ms()
    };
}

}  // namespace arb