
#include "boost/url.hpp"
#include "command_line_parser.hpp"
#include "logger.hpp"
#include "webclient.hpp"

#include <boost/asio/experimental/awaitable_operators.hpp>

#include <ranges>
#include <regex>
#include <vector>
using namespace boost::asio::experimental::awaitable_operators;

net::awaitable<void> run_tcp_client(net::io_context& ioc, std::string_view ep,
                                    std::string_view port)
{
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);
    WebClient<tcp::socket> client(ioc, ctx);
    client.withEndPoint(ep.data())
        .withPort(port.data())
        .withMethod(http::verb::get)
        .withTarget("/")
        .withRetries(3)
        .withHeaders({{"User-Agent", "coro-client"}})
        .withBody("")
        .then(
            [&](auto&& response) -> AwaitableResult<boost::system::error_code> {
                LOG_INFO("Response: {}", response.body());
                co_return boost::system::error_code{};
            })
        .orElse([](auto ec) -> AwaitableResult<boost::system::error_code> {
            LOG_ERROR("Error: {}", ec.message());
            co_return ec;
        });

    auto [ec] = co_await client.execute();
    LOG_INFO("Error: {}", ec.message());
}

net::awaitable<void> run_unix_client(net::io_context& ioc)
{
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();

    HttpClient<unix_domain::socket> client(ioc, ctx);
    auto ec = co_await client.connect("/tmp/http_server.sock", "");
    if (ec)
    {
        LOG_ERROR("Connect error: {}", ec.message());
        co_return;
    }

    http::request<http::string_body> req{http::verb::get, "/", 11};
    req.set(http::field::host, "localhost");
    req.set(http::field::user_agent, "coro-client");

    ec = co_await client.send_request(req);
    if (ec)
    {
        LOG_ERROR("Send request error: {}", ec.message());
        co_return;
    }

    auto [recv_ec, res] = co_await client.receive_response();
    if (recv_ec)
    {
        LOG_ERROR("Receive response error: {}", recv_ec.message());
        co_return;
    }

    LOG_INFO("Response: {}", res.body());
}
int main(int argc, const char* argv[])
{
    try
    {
        auto [target, port] =
            getArgs(parseCommandline(argc, argv), "--target,-t", "--port,-p");
        LOG_INFO("URL: {}, Port: {}", target.value_or("empty"),
                 port.value_or("empty"));
        net::io_context ioc;

        // Run the TCP client
        net::co_spawn(ioc,
                      run_tcp_client(ioc, target.value_or("www.google.com"),
                                     port.value_or("443")),
                      net::detached);

        // Run the Unix domain socket client
        // net::co_spawn(ioc, run_unix_client(ioc), net::detached);

        ioc.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
