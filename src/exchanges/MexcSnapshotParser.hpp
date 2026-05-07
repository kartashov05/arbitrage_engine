#pragma once

#include "../core/BookSnapshot.hpp"

#include <string>
#include <string_view>

namespace arb {

class MexcSnapshotParser {
public:
    [[nodiscard]] BookSnapshot parse(
        std::string_view body,
        const std::string& symbol
    ) const;
};

}  // namespace arb