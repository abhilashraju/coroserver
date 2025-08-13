#pragma once
#include "logger.hpp"
#include "webclient.hpp"
namespace NSNAME
{
static const std::map<boost::beast::http::status, const char*>
    http_error_to_string{
        {boost::beast::http::status::unauthorized, "Unauthorized"},
        {boost::beast::http::status::not_found, "Not Found"},
        {boost::beast::http::status::internal_server_error,
         "Internal Server Error"}};
struct RedfishClient
{
    struct Request
    {
        http::verb method{http::verb::get};
        std::string target;
        std::string body;
        std::map<std::string, std::string> params;
        int version{11};
        std::map<std::string, std::string> headers;
        bool keepAlive{false};

        Request& withMethod(http::verb m)
        {
            method = m;
            return *this;
        }
        Request& withTarget(const std::string& t)
        {
            target = t;
            return *this;
        }
        Request& withBody(const std::string& b)
        {
            body = b;
            return *this;
        }
        Request& withHeaders(const std::map<std::string, std::string>& h)
        {
            headers = h;
            return *this;
        }
        Request& witKeepAlive(bool ka)
        {
            keepAlive = ka;
            return *this;
        }
    };

    net::io_context& ioc;
    ssl::context& ctx;

    std::string userName;
    std::string password;
    std::string token;
    std::string host;
    std::string port{"443"};
    std::string protocol{"https"};
    std::string baseUrl() const
    {
        return protocol + "://" + host + ":" + port + "/redfish/v1";
    }
    RedfishClient(net::io_context& ioc, ssl::context& ctx) : ioc(ioc), ctx(ctx)
    {}

    RedfishClient(const RedfishClient&) = delete;
    RedfishClient& operator=(const RedfishClient&) = delete;
    RedfishClient(RedfishClient&&) = default;
    RedfishClient& operator=(RedfishClient&&) = default;
    RedfishClient& withUserName(const std::string& user)
    {
        userName = user;
        return *this;
    }
    RedfishClient& withPassword(const std::string& pass)
    {
        password = pass;
        return *this;
    }
    RedfishClient& withHost(const std::string& h)
    {
        host = h;
        return *this;
    }
    RedfishClient& withPort(const std::string& p)
    {
        port = p;
        return *this;
    }
    RedfishClient& withProtocol(const std::string& p)
    {
        protocol = p;
        return *this;
    }

    AwaitableResult<boost::system::error_code, std::string> getToken()
    {
        WebClient<beast::tcp_stream> webClient(ioc, ctx);
        webClient.withHost(host)
            .withPort(port)
            .withMethod(http::verb::post)
            .withTarget("/redfish/v1/SessionService/Sessions")
            .withHeaders({{"Content-Type", "application/json"}})
            .withBody(("{\"UserName\":\"" + userName + "\",\"Password\":\"" +
                       password + "\"}"));

        auto [ec, res] = co_await webClient.execute<Response>();
        if (ec || res.result() != boost::beast::http::status::created)
        {
            LOG_ERROR("Failed to get token: {} for user: {}",
                      ec ? ec.message() : http_error_to_string.at(res.result()),
                      userName);
            co_return std::make_tuple(
                boost::system::errc::make_error_code(
                    boost::system::errc::permission_denied),
                std::string{});
        }
        std::string token = res.base().at("X-Auth-Token");
        co_return std::make_tuple(boost::system::error_code{}, token);
    }
    AwaitableResult<Response> execute(const Request& req)
    {
        int retryCount = 0;
        while (retryCount++ < 3)
        {
            WebClient<beast::tcp_stream> webClient(ioc, ctx);
            webClient.withHost(host)
                .withPort(port)
                .withMethod(req.method)
                .withTarget(req.target)
                .withHeaders(req.headers)
                .withBody(req.body)
                .witKeepAlive(req.keepAlive);
            webClient.withHeaders({{"X-Auth-Token", token},
                                   {"Content-Type", "application/json"}});

            auto [ec, res] = co_await webClient.execute<Response>();
            if (ec)
            {
                LOG_ERROR("Error executing request: {}", ec.message());
                co_return std::make_tuple(ec, Response{});
            }
            if (http_error_to_string.find(res.result()) !=
                http_error_to_string.end())
            {
                LOG_ERROR("Request failed with status: {} for target: {}",
                          http_error_to_string.at(res.result()), req.target);
                if (res.result() == boost::beast::http::status::unauthorized)
                {
                    auto [tokenEc, newToken] = co_await getToken();
                    if (tokenEc)
                    {
                        LOG_ERROR("Failed to get token: {}", tokenEc.message());
                        co_return std::make_tuple(tokenEc, Response{});
                    }
                    token = newToken;
                    continue; // Retry with the new token
                }
            }
            co_return std::make_tuple(ec, res);
        }
    }
};
} // namespace NSNAME
