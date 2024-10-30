
#include "command_line_parser.hpp"
#include "http_client.hpp"
#include "logger.hpp"
void fail(beast::error_code ec, const char* what)
{
    LOG_ERROR("{}: {}", what, ec.message());
}

// Performs an HTTP GET and prints the response
net::awaitable<void> do_session(std::string host, std::string port,
                                std::string target, int version)
{
    // These objects perform our I/O

    auto resolver = net::use_awaitable.as_default_on(
        tcp::resolver(co_await net::this_coro::executor));
    auto stream = net::use_awaitable.as_default_on(
        beast::tcp_stream(co_await net::this_coro::executor));

    // Look up the domain name
    const auto results = resolver.resolve(host, port);

    // Set the timeout.
    stream.expires_after(std::chrono::seconds(30));

    // Make the connection on the IP address we get from a lookup
    co_await stream.async_connect(results);

    // Set up an HTTP GET request message
    http::request<http::string_body> req{http::verb::get, target, version};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "BOOST_BEAST_VERSION_STRING");

    // Set the timeout.
    stream.expires_after(std::chrono::seconds(30));

    // Send the HTTP request to the remote host
    co_await http::async_write(stream, req);

    // This buffer is used for reading and must be persisted
    beast::flat_buffer b;

    // Declare a container to hold the response
    http::response<http::dynamic_body> res;

    // Receive the HTTP response
    co_await http::async_read(stream, b, res);

    // Write the message to standard out
    std::cout << res << std::endl;

    // Gracefully close the socket
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);

    // not_connected happens sometimes
    // so don't bother reporting it.
    //
    if (ec && ec != beast::errc::not_connected)
        throw boost::system::system_error(ec, "shutdown");

    // If we get here then the connection is closed gracefully
}
net::awaitable<void> run_tcp_client(net::io_context& ioc, const std::string& ep,
                                    const std::string& port)
{
    ssl::context ctx(ssl::context::tlsv13_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);

    HttpClient<tcp::socket> client(ioc, ctx);

    auto ec = co_await client.connect(ep, port);
    if (ec)
    {
        LOG_ERROR("Connect error: {}", ec.message());
        co_return;
    }

    http::request<http::string_body> req{http::verb::get, "/", 11};
    req.set(http::field::host, ep);
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
                 port.value_or("unixdomain"));
        net::io_context ioc;

        // Run the TCP client
        net::co_spawn(ioc, run_tcp_client(ioc, "www.google.com", "443"),
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
