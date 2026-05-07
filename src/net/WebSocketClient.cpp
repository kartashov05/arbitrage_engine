#include "WebSocketClient.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

#include <openssl/ssl.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace arb {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;

using tcp = net::ip::tcp;

void WebSocketClient::run(
    const WsEndpoint& endpoint,
    MessageHandler on_message,
    std::size_t max_messages,
    const std::vector<std::string>& initial_text_messages
) const {
    if (!endpoint.tls()) {
        throw std::runtime_error("Only TLS WebSocket connections are supported");
    }

    net::io_context ioc;
    ssl::context ctx{ssl::context::tlsv12_client};

    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_peer);
    ctx.set_verify_callback(ssl::host_name_verification(endpoint.host));

    tcp::resolver resolver{ioc};

    beast::ssl_stream<beast::tcp_stream> tls_stream{ioc, ctx};

    if (!SSL_set_tlsext_host_name(
            tls_stream.native_handle(),
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

    beast::get_lowest_layer(tls_stream).connect(results);

    tls_stream.handshake(ssl::stream_base::client);

    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws{
        std::move(tls_stream)
    };

    ws.set_option(
        websocket::stream_base::timeout::suggested(beast::role_type::client)
    );

    ws.set_option(
        websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(
                    http::field::user_agent,
                    std::string("arbitrage-engine/0.1.0")
                );
            }
        )
    );

    ws.handshake(endpoint.host_header(), endpoint.target);

    for (const auto& text_message : initial_text_messages) {
        ws.text(true);
        ws.write(net::buffer(text_message));
    }

    ws.text(false);

    std::size_t message_count = 0;

    while (max_messages == 0 || message_count < max_messages) {
        beast::flat_buffer buffer;

        ws.read(buffer);

        const std::string message =
            beast::buffers_to_string(buffer.data());

        on_message(message);

        ++message_count;
    }

    beast::error_code close_ec;
    ws.close(websocket::close_code::normal, close_ec);

    beast::error_code ec;
    beast::get_lowest_layer(ws).socket().shutdown(
        tcp::socket::shutdown_both,
        ec
    );
    beast::get_lowest_layer(ws).socket().close(ec);
}

}  // namespace arb