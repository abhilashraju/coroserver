#pragma once

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
    virtual boost::asio::awaitable<nlohmann::json> getFresh(
        const std::string& target) = 0;

    virtual boost::asio::awaitable<nlohmann::json>
        get(const std::string& target) = 0;
};

class HttpRedfishProvider : public RedfishProvider
{
  public:
    HttpRedfishProvider(boost::asio::io_context& io,
                        const RedfishProviderConfig& config);

    boost::asio::awaitable<nlohmann::json> get(
        const std::string& target) override;

    // getFresh bypasses the cache — used by subscriptions to get live data
    boost::asio::awaitable<nlohmann::json> getFresh(
        const std::string& target) override;

  private:
    boost::asio::io_context& io;
    boost::asio::ssl::context sslContext;
    RedfishClient client;
    std::unordered_map<std::string, nlohmann::json> cache;
};

} // namespace NSNAME
