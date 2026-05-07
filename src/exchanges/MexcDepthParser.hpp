#pragma once

#include "../core/BookUpdate.hpp"

#include <optional>
#include <string_view>

namespace arb {

class MexcDepthParser {
public:
    std::optional<BookUpdate> parse(std::string_view message) const;
};

}  // namespace arb