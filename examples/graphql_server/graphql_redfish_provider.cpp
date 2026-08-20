#include "graphql_redfish_provider.hpp"

namespace NSNAME
{

HttpRedfishProvider::HttpRedfishProvider(boost::asio::io_context& io,
                                         const RedfishProviderConfig& config) :
    io(io),
    sslContext(boost::asio::ssl::context::tlsv12_client),
    client(io, sslContext)
{
    sslContext.set_default_verify_paths();
    sslContext.set_verify_mode(boost::asio::ssl::verify_none);

    if (!config.clientCertFile.empty() && !config.clientKeyFile.empty())
    {
        sslContext.use_certificate_chain_file(config.clientCertFile);
        sslContext.use_private_key_file(config.clientKeyFile,
                                        boost::asio::ssl::context::pem);
    }

    client.withHost(config.host)
        .withPort(config.port)
        .withProtocol(config.protocol)
        .withUserName(config.username)
        .withPassword(config.password);
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
    request.withMethod(http::verb::get).withTarget(target).witKeepAlive(false);

    auto [ec, response] = co_await client.execute(request);
    if (ec)
    {
        co_return std::unexpected("Failed Redfish request for '" + target +
                                  "': " + ec.message());
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
    RedfishClient::Request request;
    request.withMethod(http::verb::get).withTarget(target).witKeepAlive(false);

    auto [ec, response] = co_await client.execute(request);
    if (ec)
    {
        co_return std::unexpected("Failed Redfish request for '" + target +
                                  "': " + ec.message());
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
