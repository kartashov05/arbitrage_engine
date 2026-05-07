#include "HttpsClient.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <stdexcept>
#include <string>
#include <chrono>

namespace arb {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;

using tcp = net::ip::tcp;

std::string HttpsClient::get(const HttpsEndpoint& endpoint) const {
    net::io_context ioc;
    ssl::context ctx{ssl::context::tlsv12_client};

    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_peer);
    ctx.set_verify_callback(ssl::host_name_verification(endpoint.host));

    tcp::resolver resolver{ioc};
    beast::ssl_stream<beast::tcp_stream> stream{ioc, ctx};

    if (!SSL_set_tlsext_host_name(
            stream.native_handle(),
            endpoint.host.c_str()
        )) {
        throw beast::system_error(
            beast::error_code(
                static_cast<int>(::ERR_get_error()),
                net::error::get_ssl_category()
            )
        );
    }

    const auto results = resolver.resolve(endpoint.host, endpoint.port);

    beast::get_lowest_layer(stream).expires_after(std::chrono::seconds{10});
    beast::get_lowest_layer(stream).connect(results);

    beast::get_lowest_layer(stream).expires_after(std::chrono::seconds{10});
    stream.handshake(ssl::stream_base::client);

    http::request<http::empty_body> request{
        http::verb::get,
        endpoint.target,
        11
    };

    request.set(http::field::host, endpoint.host_header());
    request.set(http::field::user_agent, "arbitrage-engine/0.1.0");
    request.set(http::field::accept, "application/json");

    beast::get_lowest_layer(stream).expires_after(std::chrono::seconds{10});
    http::write(stream, request);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;

    beast::get_lowest_layer(stream).expires_after(std::chrono::seconds{10});
    http::read(stream, buffer, response);

    beast::error_code ec;

    beast::get_lowest_layer(stream).socket().shutdown(tcp::socket::shutdown_both, ec);
    beast::get_lowest_layer(stream).socket().close(ec);

    if (response.result() != http::status::ok) {
        throw std::runtime_error(
            "HTTP GET failed. Status: " +
            std::to_string(static_cast<unsigned>(response.result()))
        );
    }

    return response.body();
}

}  // namespace arb