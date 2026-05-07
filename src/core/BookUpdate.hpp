#pragma once

#include "Exchange.hpp"
#include "PriceLevel.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace arb {

struct BookUpdate {
    Exchange exchange;
    std::string symbol;

    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;

    std::uint64_t first_update_id{};
    std::uint64_t final_update_id{};

    std::int64_t exchange_event_time_ms{};
    std::int64_t local_receive_time_ms{};
};

}  // namespace arb