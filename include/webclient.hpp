#pragma once
#include "boost/url.hpp"
#include "http_client.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
namespace NSNAME
{

// ---------------------------------------------------------------------------
// SSE streaming types
// ---------------------------------------------------------------------------

/// One SSE frame delivered by executeAsStream().
/// ec is set on transport error or EOF; data holds the raw frame text.
struct SseFrame
{
    boost::system::error_code ec;
    std::string data;
};

/// Lightweight single-producer / single-consumer mailbox built on
/// steady_timer — same wakeup pattern used by task_barrier in when_all.hpp.
/// No new dependencies: only net::steady_timer (already used everywhere).
struct SseStream
{
    explicit SseStream(net::any_io_executor exec) : timer_(exec)
    {
        // Arm to max so the first next() call suspends immediately.
        timer_.expires_at(net::steady_timer::time_point::max());
    }

    // Called by frameProducer — deposits one frame and wakes the consumer.
    void post(SseFrame frame)
    {
        slot_ = std::move(frame);
        timer_.cancel(); // wakes whoever is co_await-ing next()
    }

    // Called by the caller's while loop — suspends until post() fires.
    net::awaitable<SseFrame> next()
    {
        boost::system::error_code ec;
        co_await timer_.async_wait(
            net::redirect_error(net::use_awaitable, ec));
        // ec == operation_aborted means timer_.cancel() was called by post().
        // Re-arm for the next frame.
        timer_.expires_at(net::steady_timer::time_point::max());
        if (!slot_)
        {
            // Timer cancelled for a reason other than post() (e.g. io_context
            // shutdown) — return an EOF frame so the consumer exits cleanly.
            co_return SseFrame{boost::asio::error::eof, {}};
        }
        SseFrame frame = std::move(*slot_);
        slot_.reset();
        co_return frame;
    }

  private:
    net::steady_timer timer_;
    std::optional<SseFrame> slot_;
};

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
        std::string frameDelimiter{"\n\n"}; // used by executeAsStream()
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
    WebClient& withParams(std::map<std::string, std::string> p)
    {
        request.params = std::move(p);
        return *this;
    }
    WebClient& withFrameDelimiter(std::string delim)
    {
        request.frameDelimiter = std::move(delim);
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
    // Build the Beast Request from the current WebRequest state.
    // Extracted so both execute() and executeAsStream() share the same logic.
    Request buildRequest() const
    {
        std::string params;
        bool first = true;
        for (const auto& [key, value] : request.params)
        {
            params += (first ? "?" : "&");
            params += key + "=" + value;
            first = false;
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
        return req;
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
        Request req = buildRequest();
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

    // Open a persistent SSE connection and return a shared SseStream.
    // The caller co_await's stream->next() in a loop to receive frames.
    // The producer coroutine is spawned detached; it exits when the socket
    // closes or the SseStream is destroyed (timer cancelled on destruction).
    net::awaitable<
        std::pair<boost::system::error_code, std::shared_ptr<SseStream>>>
        executeAsStream()
    {
        // 1. Connect
        auto [ec] = co_await tryConnect();
        if (ec)
            co_return std::make_pair(ec, nullptr);

        // 2. Build and send request with SSE headers
        Request req = buildRequest();
        req.set(http::field::accept, "text/event-stream");
        req.set(http::field::cache_control, "no-cache");
        req.keep_alive(true);

        ec = co_await client.send_request(req);
        if (ec)
            co_return std::make_pair(ec, nullptr);

        // 3. Read and parse the HTTP response header properly via Beast.
        auto [hec, statusCode] = co_await client.readResponseHeader();
        if (hec)
            co_return std::make_pair(hec, nullptr);

        // 4. Check HTTP status — abort cleanly on anything other than 2xx.
        LOG_INFO("SSE response status: {}", statusCode);
        if (statusCode < 200 || statusCode >= 300)
        {
            LOG_ERROR("SSE request rejected: HTTP {}", statusCode);
            co_return std::make_pair(
                make_error_code(boost::system::errc::connection_refused),
                nullptr);
        }

        // 5. Create the mailbox and spawn the frame producer
        auto stream = std::make_shared<SseStream>(
            co_await net::this_coro::executor);
        std::string delim = request.frameDelimiter;

        net::co_spawn(co_await net::this_coro::executor,
                      frameProducer(stream, std::move(delim)),
                      net::detached);

        co_return std::make_pair(boost::system::error_code{}, stream);
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

  private:
    // Infinite read loop — runs as a detached coroutine.
    // Reads one frame per iteration and posts it to the SseStream mailbox.
    // Exits on EOF (clean server close) or any transport error, posting an
    // SseFrame with ec set so the consumer can detect termination.
    net::awaitable<void> frameProducer(std::shared_ptr<SseStream> stream,
                                       std::string delim)
    {
        while (true)
        {
            auto [rec, frame] = co_await client.readUntil(delim);

            if (rec == net::error::eof)
            {
                // Clean server close — notify consumer then stop.
                stream->post(SseFrame{rec, {}});
                co_return;
            }
            if (rec)
            {
                // Transport error — notify consumer then stop.
                stream->post(SseFrame{rec, {}});
                co_return;
            }
            stream->post(SseFrame{{}, std::move(frame)});
        }
    }
};
}