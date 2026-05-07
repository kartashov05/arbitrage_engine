#pragma once

#include <stdexcept>
#include <string>

namespace arb {

struct HttpsEndpoint {
    std::string scheme;
    std::string host;
    std::string port;
    std::string target;

    [[nodiscard]] std::string host_header() const {
        return host + ":" + port;
    }
};

inline HttpsEndpoint make_https_endpoint(
    const std::string& base_url,
    const std::string& target
) {
    const auto scheme_pos = base_url.find("://");

    if (scheme_pos == std::string::npos) {
        throw std::runtime_error("Invalid HTTPS URL: missing scheme");
    }

    HttpsEndpoint endpoint;

    endpoint.scheme = base_url.substr(0, scheme_pos);

    if (endpoint.scheme != "https") {
        throw std::runtime_error("Only https:// endpoints are supported");
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
        throw std::runtime_error("Invalid HTTPS URL: empty host");
    }

    if (endpoint.port.empty()) {
        throw std::runtime_error("Invalid HTTPS URL: empty port");
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