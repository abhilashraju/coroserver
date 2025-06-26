#pragma once
#include "boost/url.hpp"
#include "http_client.hpp"

#include <nlohmann/json.hpp>

#include <map>
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
    struct TcpData
    {
        std::string host;
        std::string port;
    };
    struct UnixData
    {
        std::string path;
    };
    HttpClient<Stream> client;

    std::variant<TcpData, UnixData> data;

    struct WebRequest
    {
        http::verb method{http::verb::get};
        std::string target;
        std::string body;
        std::map<std::string, std::string> params;
        int version{11};
        std::map<std::string, std::string> headers;
        bool keepAlive{true};
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
        if constexpr (std::is_same_v<Stream, beast::tcp_stream>)
        {
            data = TcpData{};
        }
        else if constexpr (std::is_same_v<Stream, unix_domain::socket>)
        {
            data = UnixData{};
        }
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

    WebClient& withHost(const std::string& h)
    {
        static_assert(std::is_same_v<Stream, beast::tcp_stream>);
        std::get<TcpData>(data).host = h;
        return *this;
    }
    WebClient& witKeepAlive(bool keepAlive)
    {
        request.keepAlive = keepAlive;
        return *this;
    }

    WebClient& withPort(const std::string& p)
    {
        static_assert(std::is_same_v<Stream, beast::tcp_stream>);
        std::get<TcpData>(data).port = p;
        return *this;
    }

    WebClient& withName(const std::string& p)
    {
        static_assert(std::is_same_v<Stream, unix_domain::socket>);
        std::get<UnixData>(data).path = p;
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
    WebClient& withJsonBody(const nlohmann::json& b)
    {
        request.body = b.dump();
        return *this;
    }
    template <typename TypeBody>
    WebClient& withBody(const TypeBody& b)
    {
        nlohmann::json j = b;
        request.body = j.dump();
        return *this;
    }
    WebClient& withVersion(int v)
    {
        request.version = v;
        return *this;
    }
    WebClient& withUrl(boost::urls::url_view url)
    {
        if constexpr (std::is_same_v<Stream, beast::tcp_stream>)
        {
            withHost(url.host());
            withPort(url.port().empty() ? "443" : url.port());
        }
        else if constexpr (std::is_same_v<Stream, unix_domain::socket>)
        {
            static_assert(0, "Unix domain socket does not support url view");
        }

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
            if (std::is_same_v<Stream, beast::tcp_stream>)
            {
                auto& tcpData = std::get<TcpData>(data);
                LOG_INFO("Trying {} connection to {}:{} ", i, tcpData.host,
                         tcpData.port);

                ec = co_await client.connect(tcpData.host, tcpData.port);
            }
            else if (std::is_same_v<Stream, unix_domain::socket>)
            {
                auto& unixData = std::get<UnixData>(data);
                LOG_INFO("Trying {} connection to {} ", i, unixData.path);
                ec = co_await client.connect(unixData.path, "");
            }
            if (!ec)
            {
                isConnected = true;
                co_return ec;
            }
        }
        co_return ec;
    }
    template <typename... Ret>
    AwaitableResult<Ret...> returnFailed(boost::system::error_code ec)
    {
        constexpr int size = sizeof...(Ret);
        if constexpr (size > 1)
        {
            co_return std::make_tuple(ec, Response{});
        }
        else
        {
            co_return co_await orElseHandler(ec);
        }
    }
    template <typename... Ret>
    AwaitableResult<Ret...> returnSuccess(boost::system::error_code ec,
                                          Response response)
    {
        constexpr int size = sizeof...(Ret);
        if constexpr (size > 1)
        {
            co_return std::make_tuple(ec, std::move(response));
        }
        else
        {
            co_return co_await thenHandler(std::move(response));
        }
    }
    template <typename... Ret>
    AwaitableResult<boost::system::error_code, Ret...> execute()
    {
        auto [ec] = co_await tryConnect();
        if (ec)
        {
            co_return co_await returnFailed<boost::system::error_code, Ret...>(
                ec);
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
        req.keep_alive(request.keepAlive);
        if constexpr (std::is_same_v<Stream, beast::tcp_stream>)
        {
            req.set(http::field::host, std::get<TcpData>(data).host);
        }
        else if constexpr (std::is_same_v<Stream, unix_domain::socket>)
        {
            req.set(http::field::host, "localhost");
        }

        for (const auto& [key, value] : request.headers)
        {
            req.set(key, value);
        }
        req.body() = request.body;
        req.prepare_payload();

        ec = co_await client.send_request(req);
        if (ec)
        {
            co_return co_await returnFailed<boost::system::error_code, Ret...>(
                ec);
        }
        auto [ec1, response] = co_await client.receive_response();
        if (!ec1)
        {
            co_return co_await returnSuccess<boost::system::error_code, Ret...>(
                ec1, std::move(response));
        }
        co_return co_await returnFailed<boost::system::error_code, Ret...>(ec1);
    }
    template <typename RetType>
    AwaitableResult<boost::system::error_code, RetType> executeAndReturnAs()
    {
        static_assert(!std::is_same_v<RetType, boost::system::error_code>,
                      "Return type should not be boost::system::error_code");
        auto [ec, response] = co_await execute<Response>();
        if constexpr (std::is_same_v<RetType, Response>)
        {
            co_return std::make_tuple(ec, std::move(response));
        }
        else
        {
            if (!ec)
            {
                auto body = std::move(response.body());
                if constexpr (std::is_same_v<RetType, std::string>)
                {
                    co_return std::make_tuple(ec, body);
                }
                try
                {
                    LOG_INFO("Body: {}", body);
                    RetType val = nlohmann::json::parse(body);
                    co_return std::make_tuple(ec, std::move(val));
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("Error parsing json: {}", e.what());
                    co_return std::make_tuple(
                        make_error_code(boost::system::errc::bad_message),
                        RetType{});
                }
            }
            co_return std::make_tuple(ec, RetType{});
        }
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
