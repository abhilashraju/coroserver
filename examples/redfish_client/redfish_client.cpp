#include "redfish_client.hpp"

#include "boost/url.hpp"
#include "command_line_parser.hpp"
#include "logger.hpp"
using namespace reactor;
net::awaitable<void> makeRequest(RedfishClient& client,
                                 const boost::urls::url& url)
{
    RedfishClient::Request req;
    req.withMethod(http::verb::get)
        .withTarget(url.path().empty() ? "/" : url.path());

    auto [ec, res] = co_await client.execute(req);
    if (ec)
    {
        LOG_ERROR("Error executing request: {}", ec.message());
    }
    else
    {
        LOG_INFO("Response: {}", res.body());
    }
    co_return;
}

int main(int argc, const char* argv[])
{
    reactor::getLogger().setLogLevel(reactor::LogLevel::DEBUG);
    try
    {
        auto [user, pswd, ep] =
            getArgs(parseCommandline(argc, argv), "--user,-u", "--password,-p",
                    "--target,-t");

        if (!ep.has_value())
        {
            LOG_ERROR("Usage: redfishclient --url|-u <url>");
            LOG_ERROR(
                "Usage: redfishclient -u <user> -p <pswd> -t https://host::port/redfish/v1");
            return EXIT_FAILURE;
        }
        std::string url_str(ep.value());

        net::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);

        // Load the root certificates
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_none);
        RedfishClient client(ioc, ctx);
        boost::urls::url url = boost::urls::parse_uri(url_str).value();
        client.withHost(url.host())
            .withPort(url.port().empty() ? "443" : url.port())
            .withProtocol("https")
            .withUserName(user.value_or("admin").data())
            .withPassword(pswd.value_or("password").data());
        // Run the TCP client
        net::co_spawn(ioc, std::bind_front(makeRequest, std::ref(client), url),
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
