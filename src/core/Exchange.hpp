#pragma once

#include <stdexcept>
#include <string>

namespace arb {

enum class Exchange {
    Binance,
    Mexc
};

inline std::string to_string(Exchange exchange) {
    switch (exchange) {
        case Exchange::Binance:
            return "binance";
        case Exchange::Mexc:
            return "mexc";
    }

    throw std::runtime_error("Unknown exchange");
}

}  // namespace arb