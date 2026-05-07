#pragma once

#include "WsEndpoint.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace arb {

class WebSocketClient {
public:
    using MessageHandler = std::function<void(std::string_view)>;

    WebSocketClient() = default;

    void run(
        const WsEndpoint& endpoint,
        MessageHandler on_message,
        std::size_t max_messages = 0,
        const std::vector<std::string>& initial_text_messages = {}
    ) const;

    static void request_stop_all();
};

}  // namespace arb