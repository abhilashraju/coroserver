#pragma once

#include "graphql/error.hpp"
#include "name_space.hpp"
#include "redfish_client.hpp"

#include <boost/asio/ssl/context.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace NSNAME
{

struct RedfishProviderConfig
{
    std::string host{"localhost"};
    std::string port{"443"};
    std::string protocol{"https"};
    // Basic-auth credentials (optional when mTLS is used)
    std::string username;
    std::string password;
    // mTLS client certificate/key paths (optional when basic-auth is used)
    std::string clientCertFile;
    std::string clientKeyFile;
};

class RedfishProvider
{
  public:
    virtual ~RedfishProvider() = default;
    virtual boost::asio::awaitable<NSNAME::graphql::Result<nlohmann::json>>
        getFresh(const std::string& target) = 0;

    virtual boost::asio::awaitable<NSNAME::graphql::Result<nlohmann::json>> get(
        const std::string& target) = 0;
};

class HttpRedfishProvider : public RedfishProvider
{
  public:
    HttpRedfishProvider(boost::asio::io_context& io,
                        const RedfishProviderConfig& config);

    boost::asio::awaitable<NSNAME::graphql::Result<nlohmann::json>> get(
        const std::string& target) override;

    // getFresh bypasses the cache — used by subscriptions to get live data.
    // All calls share the provider-owned connection pool so keep-alive
    // connections are reused across both query and subscription requests.
    boost::asio::awaitable<NSNAME::graphql::Result<nlohmann::json>> getFresh(
        const std::string& target) override;

  private:
    boost::asio::ssl::context sslContext;
    RedfishProviderConfig config;
    // Single connection pool shared by all requests (get and getFresh).
    // Connections are kept alive and reused up to maxConnectionsPerHost.
    std::shared_ptr<ConnectionPool<beast::tcp_stream>> pool;
    // client holds credentials and token; pool_ inside it points to pool
    // above so every WebClient constructed from it shares the same connections.
    RedfishClient client;
    std::unordered_map<std::string, nlohmann::json> cache;
};

} // namespace NSNAME
