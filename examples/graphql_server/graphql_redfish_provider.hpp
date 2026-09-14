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
    // Each call constructs its own RedfishClient on the coroutine frame so
    // concurrent subscription loops never share a TCP connection.
    boost::asio::awaitable<NSNAME::graphql::Result<nlohmann::json>> getFresh(
        const std::string& target) override;

  private:
    // Build a fully configured RedfishClient bound to the shared token.
    // The returned client lives on the caller's coroutine frame and is never
    // shared with any other concurrent coroutine.
    RedfishClient makeClient();

    boost::asio::io_context& io;
    boost::asio::ssl::context sslContext;
    RedfishProviderConfig config;
    // Auth token shared across clients so a single token refresh is visible
    // to all subsequent callers without re-authenticating.
    std::string sharedToken;
    // Sequential cached-read client (get() is never called concurrently).
    RedfishClient queryClient;
    std::unordered_map<std::string, nlohmann::json> cache;
};

} // namespace NSNAME
