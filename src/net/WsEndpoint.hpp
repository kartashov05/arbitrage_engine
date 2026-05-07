#pragma once

#include <stdexcept>
#include <string>

namespace arb {

struct WsEndpoint {
    std::string scheme;
    std::string host;
    std::string port;
    std::string target;

    [[nodiscard]] std::string host_header() const {
        if (port == "443") {
            return host;
        }
    
        return host + ":" + port;
    }

    [[nodiscard]] bool tls() const {
        return scheme == "wss";
    }
};

inline WsEndpoint make_ws_endpoint(
    const std::string& base_url,
    const std::string& target
) {
    const auto scheme_pos = base_url.find("://");

    if (scheme_pos == std::string::npos) {
        throw std::runtime_error("Invalid WebSocket URL: missing scheme");
    }

    WsEndpoint endpoint;

    endpoint.scheme = base_url.substr(0, scheme_pos);

    if (endpoint.scheme != "wss") {
        throw std::runtime_error("Only wss:// WebSocket endpoints are supported for now");
    }

    std::string rest = base_url.substr(scheme_pos + 3);

    const auto slash_pos = rest.find('/');
    const std::string host_port =
        slash_pos == std::string::npos ? rest : rest.substr(0, slash_pos);

    const auto colon_pos = host_port.find(':');

    if (colon_pos == std::string::npos) {
        endpoint.host = host_port;
        endpoint.port = "443";
    } else {
        endpoint.host = host_port.substr(0, colon_pos);
        endpoint.port = host_port.substr(colon_pos + 1);
    }

    if (endpoint.host.empty()) {
        throw std::runtime_error("Invalid WebSocket URL: empty host");
    }

    if (endpoint.port.empty()) {
        throw std::runtime_error("Invalid WebSocket URL: empty port");
    }

    if (target.empty()) {
        endpoint.target = "/";
    } else if (target.front() == '/') {
        endpoint.target = target;
    } else {
        endpoint.target = "/" + target;
    }

    return endpoint;
}

}  // namespace arb