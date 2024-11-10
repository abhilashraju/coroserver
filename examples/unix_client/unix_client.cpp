#include "boost/url.hpp"
#include "command_line_parser.hpp"
#include "logger.hpp"
#include "webclient.hpp"

#include <vector>

net::awaitable<void> run_unix_client(net::io_context& ioc,
                                     std::string_view name)
{
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);
    WebClient<unix_domain::socket> client(ioc, ctx);

    client.withName(name.data())
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
        auto [name] = getArgs(parseCommandline(argc, argv), "--name,-n");

        if (!name.has_value())
        {
            LOG_ERROR("Usage: client --name|-n <unix socket path name>");

            return EXIT_FAILURE;
        }
        net::io_context ioc;
        net::co_spawn(
            ioc, run_unix_client(ioc, name.value_or("/tmp/http_server.sock")),
            net::detached);

        ioc.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
