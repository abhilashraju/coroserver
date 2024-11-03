#pragma once
#include "boost/url.hpp"
#include "http_client.hpp"

template <typename T>
concept WebClientThenFunction =
    requires(T t, Response response) {
        {
            t(response)
        } -> std::same_as<AwaitableResult<boost::system::error_code>>;
    };

template <typename T>
concept WebClientOrElseFunction =
    requires(T t, boost::system::error_code ec) {
        { t(ec) } -> std::same_as<AwaitableResult<boost::system::error_code>>;
    };
template <typename Stream>
struct WebClient
{
    HttpClient<Stream> client;
    std::string host;
    std::string port{"443"};
    struct WebRequest
    {
        http::verb method{http::verb::get};
        std::string target;
        std::string body;
        std::map<std::string, std::string> params;
        int version{11};
        std::map<std::string, std::string> headers;
    } request;
    std::function<AwaitableResult<boost::system::error_code>(Response)>
        thenHandler;

    std::function<AwaitableResult<boost::system::error_code>(
        boost::system::error_code)>
        orElseHandler;

    struct RetryPolicy
    {
        int maxTries{3};
    };
    RetryPolicy retryPolicy;
    bool isConnected{false};
    WebClient(net::io_context& ioc, ssl::context& ctx) : client(ioc, ctx)
    {
        thenHandler = [](Response response)
            -> AwaitableResult<boost::system::error_code> {
            LOG_INFO("Response: {}", response.body());
            co_return boost::system::error_code{};
        };
        orElseHandler = [](boost::system::error_code ec)
            -> AwaitableResult<boost::system::error_code> { co_return ec; };
    }

    WebClient(const WebClient&) = delete;
    WebClient& operator=(const WebClient&) = delete;
    WebClient(WebClient&&) = default;
    WebClient& operator=(WebClient&&) = default;
    WebClient& withEndPoint(const std::string& h)
    {
        host = h;
        return *this;
    }
    WebClient& withPort(const std::string& p)
    {
        port = p;
        return *this;
    }
    WebClient& withRetries(int maxRetries)
    {
        retryPolicy.maxTries = maxRetries;
        return *this;
    }
    WebClient& withMethod(http::verb m)
    {
        request.method = m;
        return *this;
    }
    WebClient& withTarget(const std::string& t)
    {
        request.target = t;
        return *this;
    }
    WebClient& withHeaders(std::map<std::string, std::string> h)
    {
        request.headers = std::move(h);
        return *this;
    }
    WebClient& withBody(std::string b)
    {
        request.body = std::move(b);
        return *this;
    }
    WebClient& withVersion(int v)
    {
        request.version = v;
        return *this;
    }
    WebClient& withUrl(boost::urls::url_view url)
    {
        host = url.host();
        port = url.port().empty() ? "443" : url.port();
        request.target = url.path().empty() ? "/" : url.path();
        for (auto [key, value, ex] : url.params())
        {
            request.params[key] = value;
        }
        return *this;
    }
    AwaitableResult<boost::system::error_code> tryConnect()
    {
        if (isConnected)
        {
            co_return boost::system::error_code{};
        }
        boost::system::error_code ec{};
        for (int i = 0; i < retryPolicy.maxTries; i++)
        {
            LOG_INFO("Trying {} connection to {}:{} ", i, host, port);
            ec = co_await client.connect(host, port);
            if (!ec)
            {
                co_return ec;
            }
        }
        co_return ec;
    }
    AwaitableResult<boost::system::error_code> execute()
    {
        auto [ec] = co_await tryConnect();
        if (ec)
        {
            co_return co_await orElseHandler(ec);
        }
        std::string params;
        for (const auto& [key, value] : request.params)
        {
            params += key + "=" + value + "&";
        }
        if (!params.empty())
        {
            params = "?" + params;
        }
        Request req(request.method, request.target + params, request.version);
        req.set(http::field::host, host);

        for (const auto& [key, value] : request.headers)
        {
            req.set(key, value);
        }
        req.body() = request.body;
        req.prepare_payload();

        ec = co_await client.send_request(req);
        if (ec)
        {
            co_return co_await orElseHandler(ec);
        }
        auto [ec1, response] = co_await client.receive_response();
        if (!ec1)
        {
            co_return co_await thenHandler(std::move(response));
        }
        co_return co_await orElseHandler(ec1);
    }
    WebClient& then(WebClientThenFunction auto handler)
    {
        thenHandler = std::move(handler);
        return *this;
    }
    WebClient& orElse(WebClientOrElseFunction auto handler)
    {
        orElseHandler = std::move(handler);
        return *this;
    };
};
