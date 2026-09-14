#include "graphql_redfish_provider.hpp"

namespace NSNAME
{

HttpRedfishProvider::HttpRedfishProvider(boost::asio::io_context& io,
                                         const RedfishProviderConfig& cfg) :
    io(io), sslContext(boost::asio::ssl::context::tlsv12_client), config(cfg),
    queryClient(io, sslContext)
{
    sslContext.set_default_verify_paths();
    sslContext.set_verify_mode(boost::asio::ssl::verify_none);

    if (!config.clientCertFile.empty() && !config.clientKeyFile.empty())
    {
        sslContext.use_certificate_chain_file(config.clientCertFile);
        sslContext.use_private_key_file(config.clientKeyFile,
                                        boost::asio::ssl::context::pem);
    }

    queryClient.withHost(config.host)
        .withPort(config.port)
        .withProtocol(config.protocol)
        .withUserName(config.username)
        .withPassword(config.password);
}

RedfishClient HttpRedfishProvider::makeClient()
{
    RedfishClient c(io, sslContext);
    c.withHost(config.host)
        .withPort(config.port)
        .withProtocol(config.protocol)
        .withUserName(config.username)
        .withPassword(config.password);
    // Seed the cached token so the first call avoids a round-trip if we
    // already authenticated. The client will refresh automatically on 401.
    c.token = sharedToken;
    return c;
}

boost::asio::awaitable<NSNAME::graphql::Result<nlohmann::json>>
    HttpRedfishProvider::get(const std::string& target)
{
    auto cached = cache.find(target);
    if (cached != cache.end())
    {
        co_return cached->second;
    }

    RedfishClient::Request request;
    request.withMethod(http::verb::get).withTarget(target);

    // queryClient is used only by get(), which is called sequentially during
    // initial query execution — never concurrently with itself.
    auto [ec, response] = co_await queryClient.execute(request);
    if (ec)
    {
        co_return std::unexpected(
            "Failed Redfish request for '" + target + "': " + ec.message());
    }

    nlohmann::json parsed =
        nlohmann::json::parse(response.body(), nullptr, false);
    if (parsed.is_discarded())
    {
        co_return std::unexpected("Invalid JSON response for '" + target + "'");
    }

    cache.emplace(target, parsed);
    co_return parsed;
}

boost::asio::awaitable<NSNAME::graphql::Result<nlohmann::json>>
    HttpRedfishProvider::getFresh(const std::string& target)
{
    // Each getFresh() call owns its own RedfishClient on this coroutine's
    // stack frame. Concurrent subscription polling loops therefore each have
    // an independent TCP+TLS connection — no shared mutable socket state,
    // no races on isConnected or Beast buffers.
    RedfishClient localClient = makeClient();

    RedfishClient::Request request;
    request.withMethod(http::verb::get).withTarget(target);

    auto [ec, response] = co_await localClient.execute(request);

    // Propagate a refreshed token back to the shared slot so later makeClient()
    // calls (and the queryClient) benefit from it without re-authenticating.
    if (!localClient.token.empty())
    {
        sharedToken = localClient.token;
        queryClient.token = sharedToken;
    }

    if (ec)
    {
        co_return std::unexpected(
            "Failed Redfish request for '" + target + "': " + ec.message());
    }

    nlohmann::json parsed =
        nlohmann::json::parse(response.body(), nullptr, false);
    if (parsed.is_discarded())
    {
        co_return std::unexpected("Invalid JSON response for '" + target + "'");
    }

    co_return parsed;
}

} // namespace NSNAME
