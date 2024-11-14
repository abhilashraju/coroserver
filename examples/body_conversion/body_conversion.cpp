#include "boost/url.hpp"
#include "command_line_parser.hpp"
#include "logger.hpp"
#include "webclient.hpp"

#include <vector>
struct Person
{
    std::string name;
    int age;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Person, name, age)
};
net::awaitable<void> run_unix_client(
    net::io_context& ioc, std::string_view name, std::string_view target)
{
    ssl::context ctx(ssl::context::tlsv12_client);

    // Load the root certificates
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_none);
    WebClient<unix_domain::socket> client(ioc, ctx);

    client.withName(name.data())
        .withMethod(http::verb::post)
        .withTarget(target.data())
        .withRetries(3)
        .withHeaders({{"User-Agent", "coro-client"}})
        // .withJsonBody(nlohmann::json({{"userName", "test"}}));
        .withBody(Person{"test", 20});
    auto [ec, res] = co_await client.executeAndReturnAs<std::string>();

    LOG_INFO("Error: {} {}", ec.message(), res);
}

int main(int argc, const char* argv[])
{
    try
    {
        auto [name, target] =
            getArgs(parseCommandline(argc, argv), "--name,-n", "--target,-t");

        net::io_context ioc;
        net::co_spawn(
            ioc,
            run_unix_client(ioc, name.value_or("/tmp/http_server.sock"),
                            target.value_or("/")),
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
