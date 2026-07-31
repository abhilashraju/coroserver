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
    std::string username;
    std::string password;
};

class RedfishProvider
{
  public:
    virtual ~RedfishProvider() = default;
    virtual boost::asio::awaitable<nlohmann::json>
        get(const std::string& target) = 0;
};

class HttpRedfishProvider : public RedfishProvider
{
  public:
    HttpRedfishProvider(boost::asio::io_context& io,
                        const RedfishProviderConfig& config);

    boost::asio::awaitable<nlohmann::json> get(const std::string& target) override;

  private:
    boost::asio::io_context& io;
    boost::asio::ssl::context sslContext;
    RedfishClient client;
    std::unordered_map<std::string, nlohmann::json> cache;
};

} // namespace NSNAME
