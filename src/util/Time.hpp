#pragma once

#include <chrono>
#include <cstdint>

namespace arb {

inline std::int64_t now_ms() {
    const auto now = std::chrono::system_clock::now();
    const auto duration = now.time_since_epoch();

    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

}  // namespace arb