#pragma once

#include "HttpsEndpoint.hpp"

#include <string>

namespace arb {

class HttpsClient {
public:
    HttpsClient() = default;

    [[nodiscard]] std::string get(const HttpsEndpoint& endpoint) const;
};

}  // namespace arb