#pragma once

#include "Exchange.hpp"
#include "PriceLevel.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace arb {

struct BookSnapshot {
    Exchange exchange;
    std::string symbol;

    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;

    std::uint64_t last_update_id{};
    std::int64_t local_receive_time_ms{};
};

}  // namespace arb