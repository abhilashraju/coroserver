
#include "boost/url.hpp"
#include "command_line_parser.hpp"
#include "logger.hpp"
#include "webclient.hpp"
using namespace NSNAME;
net::awaitable<void> run_tcp_client(net::io_context& ioc, std::string_view ep,
                                    std::string_view port)
{
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);
    WebClient<beast::tcp_stream> client(ioc, ctx);

    client.withHost(ep.data())
        .withPort(port.data())
        .withMethod(http::verb::get)
        .withTarget("/")
        .withRetries(3)
        .withHeaders({{"User-Agent", "coro-client"}});
    auto [ec, res] = co_await client.execute<Response>();

    LOG_INFO("Error: {} {}", ec.message(), res.body());
}
net::awaitable<void> run_unix_client(net::io_context& ioc, std::string_view ep,
                                     std::string_view port)
{
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);
    WebClient<unix_domain::socket> client(ioc, ctx);

    client.withName("/tmp/http_server.sock")
        .withMethod(http::verb::get)
        .withTarget("/")
        .withRetries(3)
        .withHeaders({{"User-Agent", "coro-client"}});
    auto [ec, res] = co_await client.execute<Response>();

    LOG_INFO("Error: {} {}", ec.message(), res.body());
}

int main(int argc, const char* argv[])
{
    try
    {
        auto [domain, port, name] =
            getArgs(parseCommandline(argc, argv), "--domain,-d", "--port,-p",
                    "--name,-n");

        if (!domain.has_value() && !name.has_value())
        {
            LOG_ERROR(
                "Usage: web_client --domain|-d <domain end point> --name|-n <unix socket path name>");
            LOG_ERROR("Usage: web_client -d https://www.google.com -p 443");

            return EXIT_FAILURE;
        }
        net::io_context ioc;

        // Run the TCP client
        if (domain.has_value())
        {
            boost::urls::url url =
                boost::urls::parse_uri(domain.value().data()).value();
            net::co_spawn(ioc,
                          run_tcp_client(ioc, url.host(), port.value_or("443")),
                          net::detached);
        }
        if (name.has_value())
        {
            net::co_spawn(
                ioc,
                run_unix_client(ioc, name.value_or("/tmp/http_server.sock"),
                                port.value_or("")),
                net::detached);
        }

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
