#include "redfish_client.hpp"

#include "boost/url.hpp"
#include "command_line_parser.hpp"
#include "logger.hpp"
#include "when_all.hpp"

#include <memory>
#include <vector>

using namespace reactor;

// Result structure for request responses
struct RequestResult
{
    std::string name;
    boost::system::error_code ec;
    Response response;
};

net::awaitable<RequestResult> makeRequest(
    RedfishClient& client, boost::urls::url url, std::string name)
{
    RedfishClient::Request req;
    req.withMethod(http::verb::get)
        .withTarget(url.path().empty() ? "/" : url.path());

    auto [ec, res] = co_await client.execute(req);

    co_return RequestResult{std::move(name), ec, std::move(res)};
}

net::awaitable<void> makeConcurrentRequests(RedfishClient& client,
                                            const boost::urls::url& baseUrl,
                                            net::io_context& ioc)
{
    // Define 5 URLs to fetch concurrently
    std::vector<std::pair<std::string, std::string>> urls = {
        {"/redfish/v1", "ServiceRoot"},
        {"/redfish/v1/Systems", "Systems"},
        {"/redfish/v1/Chassis", "Chassis"},
        {"/redfish/v1/Managers", "Managers"},
        {"/redfish/v1/SessionService", "SessionService"}};

    LOG_INFO("Launching {} concurrent requests using when_all (vector version)",
             urls.size());

    // Create vector of awaitables
    std::vector<net::awaitable<RequestResult>> awaitables;
    for (const auto& [path, name] : urls)
    {
        boost::urls::url url = baseUrl;
        url.set_path(path);
        awaitables.push_back(makeRequest(client, url, name));
    }

    // Execute all requests in parallel using when_all with vector
    auto results = co_await when_all(std::move(awaitables));

    LOG_INFO("All {} concurrent requests completed", results.size());

    // Log results
    for (const auto& result : results)
    {
        if (result.ec)
        {
            LOG_INFO("Request '{}' failed: {}", result.name,
                     result.ec.message());
        }
        else
        {
            LOG_INFO("Response for {}: {}", result.name,
                     result.response.body());
        }
    }

    co_return;
}

net::awaitable<void> makeConcurrentRequestsVariadic(
    RedfishClient& client, const boost::urls::url& baseUrl,
    net::io_context& ioc)
{
    LOG_INFO(
        "Launching 5 concurrent requests using when_all (variadic version)");

    // Create individual awaitables for each request

    auto request1 =
        makeRequest(client, boost::urls::url(baseUrl).set_path("/redfish/v1"),
                    "ServiceRoot");

    auto request2 = makeRequest(
        client, boost::urls::url(baseUrl).set_path("/redfish/v1/Systems"),
        "Systems");

    auto request3 = makeRequest(
        client, boost::urls::url(baseUrl).set_path("/redfish/v1/Chassis"),
        "Chassis");

    auto request4 = makeRequest(
        client, boost::urls::url(baseUrl).set_path("/redfish/v1/Managers"),
        "Managers");

    boost::urls::url url5 = baseUrl;
    url5.set_path("/redfish/v1/SessionService");
    auto request5 = makeRequest(
        client,
        boost::urls::url(baseUrl).set_path("/redfish/v1/SessionService"),
        "SessionService");

    // Execute all requests in parallel using when_all with variadic parameters
    // Returns a tuple of results
    auto [result1, result2, result3, result4, result5] = co_await when_all(
        std::move(request1), std::move(request2), std::move(request3),
        std::move(request4), std::move(request5));

    LOG_INFO("All 5 concurrent requests completed (variadic version)");

    // Log results using structured bindings
    std::array<std::reference_wrapper<const RequestResult>, 5> results = {
        result1, result2, result3, result4, result5};

    for (const auto& result : results)
    {
        if (result.get().ec)
        {
            LOG_INFO("Request '{}' failed: {}", result.get().name,
                     result.get().ec.message());
        }
        else
        {
            LOG_INFO("Response for {}: {}", result.get().name,
                     result.get().response.body());
        }
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
        // Run both types of concurrent requests using when_all
        net::co_spawn(
            ioc,
            [&]() -> net::awaitable<void> {
                LOG_INFO("Running both vector and variadic when_all examples "
                         "concurrently");

                // Use when_all to run both examples concurrently
                co_await when_all(
                    makeConcurrentRequests(std::ref(client), url, ioc),
                    makeConcurrentRequestsVariadic(std::ref(client), url, ioc));

                LOG_INFO("Both when_all examples completed successfully");
            }(),
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
