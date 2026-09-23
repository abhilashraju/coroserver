#pragma once
#include "boost/url.hpp"
#include "connection_pool.hpp"
#include "http_client.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>

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
        co_await timer_.async_wait(net::redirect_error(net::use_awaitable, ec));
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

// ---------------------------------------------------------------------------
// Body-type tags — select the response body strategy via as<Tag>()
// ---------------------------------------------------------------------------

/// Tag: receive the response body as a std::string (default, existing path).
struct StringBodyTag
{};

/// Tag: stream the response body directly into a file on disk.
struct FileBodyTag
{};

/// Tag: receive the response body into a dynamic_body flat buffer.
struct BufferBodyTag
{};

/**
 * @brief HTTP/HTTPS/Unix client builder and executor backed by ConnectionPool.
 *
 * All requests lease connections from a ConnectionPool (with keep-alive reuse,
 * idle connection expiry, and per-host limits), exactly like Java's HttpClient.
 */
template <typename Stream>
struct WebClient
{
    struct TcpData
    {
        std::string host;
        std::string port{"443"};
    };
    struct UnixData
    {
        std::string path;
    };

    std::variant<TcpData, UnixData> data;

    struct WebRequest
    {
        http::verb method{http::verb::get};
        std::string target{"/"};
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

    std::shared_ptr<ConnectionPool<Stream>> pool_;

    /// Construct WebClient with an explicit shared ConnectionPool instance
    explicit WebClient(std::shared_ptr<ConnectionPool<Stream>> pool) :
        pool_(std::move(pool))
    {
        initDefaults();
    }

    /// Construct WebClient with io_context and ssl_context, creating or using a
    /// pool
    WebClient(net::io_context& ioc, ssl::context& ctx,
              ConnectionPoolConfig poolConfig = {}) :
        pool_(std::make_shared<ConnectionPool<Stream>>(ioc, ctx, poolConfig))
    {
        initDefaults();
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

    WebClient& withKeepAlive(bool keepAlive)
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

    std::pair<std::string, std::string> getEndpoint() const
    {
        if constexpr (std::is_same_v<Stream, beast::tcp_stream>)
        {
            const auto& tcpData = std::get<TcpData>(data);
            return {tcpData.host, tcpData.port};
        }
        else if constexpr (std::is_same_v<Stream, unix_domain::socket>)
        {
            const auto& unixData = std::get<UnixData>(data);
            return {unixData.path, ""};
        }
    }

    /**
     * @brief Executes the request by leasing a connection from the
     * ConnectionPool. On completion, if the connection is healthy and
     * keep-alive is active, the lease automatically returns the connection to
     * the pool for reuse.
     */
    template <typename... Ret>
    AwaitableResult<boost::system::error_code, Ret...> execute()
    {
        auto [hostOrPath, port] = getEndpoint();
        Request req = buildRequest();

        boost::system::error_code lastEc{};
        int maxAttempts = std::max(1, retryPolicy.maxTries);

        for (int attempt = 0; attempt < maxAttempts; ++attempt)
        {
            auto [acqEc, lease] = co_await pool_->acquire(hostOrPath, port);
            if (acqEc)
            {
                lastEc = acqEc;
                LOG_INFO("Retrying ({}/{}) connection to {}", attempt + 1,
                         maxAttempts, hostOrPath);
                continue;
            }

            boost::system::error_code sendEc =
                co_await lease.get().send_request(req);
            if (sendEc)
            {
                // Socket closed or failed, mark lease invalid so it is dropped
                lease.markInvalid();
                lastEc = sendEc;
                LOG_INFO("Send failed, retrying ({}/{}) to {}", attempt + 1,
                         maxAttempts, hostOrPath);
                continue;
            }

            auto [recvEc, response] = co_await lease.get().receive_response();
            if (recvEc)
            {
                lease.markInvalid();
                lastEc = recvEc;
                LOG_INFO("Receive failed, retrying ({}/{}) to {}", attempt + 1,
                         maxAttempts, hostOrPath);
                continue;
            }

            // Check if server or request asked to close the connection
            if (!response.keep_alive() || !request.keepAlive)
            {
                lease.markInvalid();
            }

            co_return co_await returnSuccess<boost::system::error_code, Ret...>(
                recvEc, std::move(response));
        }

        co_return co_await returnFailed<boost::system::error_code, Ret...>(
            lastEc);
    }

    // Open an SSE connection by acquiring a connection and holding it for
    // streaming.
    net::awaitable<
        std::pair<boost::system::error_code, std::shared_ptr<SseStream>>>
        executeAsStream()
    {
        auto [hostOrPath, port] = getEndpoint();

        // 1. Acquire connection from pool
        auto [acqEc, lease] = co_await pool_->acquire(hostOrPath, port);
        if (acqEc)
        {
            co_return std::make_pair(acqEc, nullptr);
        }

        // 2. Build and send request with SSE headers
        Request req = buildRequest();
        req.set(http::field::accept, "text/event-stream");
        req.set(http::field::cache_control, "no-cache");
        req.keep_alive(true);

        boost::system::error_code ec = co_await lease.get().send_request(req);
        if (ec)
        {
            lease.markInvalid();
            co_return std::make_pair(ec, nullptr);
        }

        // 3. Read and parse the HTTP response header properly via Beast
        auto [hec, statusCode] = co_await lease.get().readResponseHeader();
        if (hec)
        {
            lease.markInvalid();
            co_return std::make_pair(hec, nullptr);
        }

        // 4. Check HTTP status — abort cleanly on anything other than 2xx
        LOG_INFO("SSE response status: {}", statusCode);
        if (statusCode < 200 || statusCode >= 300)
        {
            lease.markInvalid();
            LOG_ERROR("SSE request rejected: HTTP {}", statusCode);
            co_return std::make_pair(
                make_error_code(boost::system::errc::connection_refused),
                nullptr);
        }

        // 5. Transfer ownership of client for the long-lived streaming producer
        auto client = lease.releaseClient();
        auto stream =
            std::make_shared<SseStream>(co_await net::this_coro::executor);
        std::string delim = request.frameDelimiter;

        net::co_spawn(
            co_await net::this_coro::executor,
            frameProducer(std::move(client), stream, std::move(delim)),
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
    }

    std::shared_ptr<ConnectionPool<Stream>> getPool() const
    {
        return pool_;
    }

    // -----------------------------------------------------------------------
    // Body-executor factory
    //
    // Usage:
    //   .as<FileBodyTag>("/tmp/fw.bin").execute()   → (ec, path)
    //   .as<BufferBodyTag>().execute()              → (ec,
    //   response<dynamic_body>) .execute<Ret...>()  ← existing string-body
    //   path, unchanged
    // -----------------------------------------------------------------------
    template <typename BodyTag = StringBodyTag, typename... Args>
    auto as(Args&&... args);

  private:
    void initDefaults()
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

    // Infinite read loop for SSE — runs as a detached coroutine holding the
    // client socket.
    static net::awaitable<void> frameProducer(
        std::unique_ptr<HttpClient<Stream>> client,
        std::shared_ptr<SseStream> stream, std::string delim)
    {
        while (true)
        {
            auto [rec, frame] = co_await client->readUntil(delim);

            if (rec == net::error::eof)
            {
                stream->post(SseFrame{rec, {}});
                co_return;
            }
            if (rec)
            {
                stream->post(SseFrame{rec, {}});
                co_return;
            }
            stream->post(SseFrame{{}, std::move(frame)});
        }
    }
};

// ---------------------------------------------------------------------------
// StringExecutor — thin wrapper that delegates to WebClient::execute<Ret...>()
// so callers that use .as() / .as<StringBodyTag>() get the familiar path.
// ---------------------------------------------------------------------------
template <typename Stream>
struct StringExecutor
{
    WebClient<Stream>& client;

    template <typename... Ret>
    auto execute()
    {
        return client.template execute<Ret...>();
    }
};

// ---------------------------------------------------------------------------
// FileExecutor — streams the response body directly to a file on disk.
// Returns (error_code, filesystem::path) so the caller can verify the path.
// ---------------------------------------------------------------------------
template <typename Stream>
struct FileExecutor
{
    WebClient<Stream>& client;
    std::filesystem::path filePath;

    net::awaitable<std::pair<boost::system::error_code, std::filesystem::path>>
        execute()
    {
        auto [hostOrPath, port] = client.getEndpoint();
        Request req = client.buildRequest();

        boost::system::error_code lastEc{};
        int maxAttempts = std::max(1, client.retryPolicy.maxTries);

        for (int attempt = 0; attempt < maxAttempts; ++attempt)
        {
            auto [acqEc,
                  lease] = co_await client.pool_->acquire(hostOrPath, port);
            if (acqEc)
            {
                lastEc = acqEc;
                LOG_INFO("FileExecutor: retrying ({}/{}) connection to {}",
                         attempt + 1, maxAttempts, hostOrPath);
                continue;
            }

            boost::system::error_code sendEc =
                co_await lease.get().send_request(req);
            if (sendEc)
            {
                lease.markInvalid();
                lastEc = sendEc;
                LOG_INFO("FileExecutor: send failed, retrying ({}/{}) to {}",
                         attempt + 1, maxAttempts, hostOrPath);
                continue;
            }

            // Read response header first to verify HTTP status before writing
            // any bytes to disk.  A non-2xx response (redirect, error page)
            // must never be written into the firmware file.
            auto [hec, statusCode] = co_await lease.get().readResponseHeader();
            if (hec)
            {
                lease.markInvalid();
                lastEc = hec;
                LOG_INFO("FileExecutor: header read failed, retrying ({}/{}) "
                         "to {}",
                         attempt + 1, maxAttempts, hostOrPath);
                continue;
            }
            if (statusCode < 200 || statusCode >= 300)
            {
                lease.markInvalid();
                LOG_ERROR("FileExecutor: server returned HTTP {} for {}",
                          statusCode, hostOrPath);
                lastEc =
                    make_error_code(boost::system::errc::connection_refused);
                continue;
            }

            // Pre-open the destination file and stream the body directly into
            // it.  readBodyAs() move-constructs from the stored header parser
            // so Beast reuses the same parser state — no double-header parse,
            // no body-limit surprise.  300s timeout for large firmware
            // tarballs.
            http::response<http::file_body> res;
            beast::error_code fec;
            res.body().open(filePath.c_str(), beast::file_mode::write, fec);
            if (fec)
            {
                lease.markInvalid();
                co_return std::make_pair(
                    static_cast<boost::system::error_code>(fec), filePath);
            }

            auto [recvEc, fileRes] =
                co_await lease.get().template readBodyAs<http::file_body>(
                    std::move(res), std::chrono::seconds(300));
            if (recvEc)
            {
                lease.markInvalid();
                lastEc = recvEc;
                LOG_INFO("FileExecutor: recv failed, retrying ({}/{}) to {}",
                         attempt + 1, maxAttempts, hostOrPath);
                continue;
            }

            if (!fileRes.keep_alive() || !client.request.keepAlive)
            {
                lease.markInvalid();
            }

            co_return std::make_pair(boost::system::error_code{}, filePath);
        }

        co_return std::make_pair(lastEc, filePath);
    }
};

// ---------------------------------------------------------------------------
// BufferExecutor — receives the response body into a dynamic_body buffer.
// Returns (error_code, http::response<http::dynamic_body>).
// ---------------------------------------------------------------------------
template <typename Stream>
struct BufferExecutor
{
    WebClient<Stream>& client;

    net::awaitable<std::pair<boost::system::error_code,
                             http::response<http::dynamic_body>>>
        execute()
    {
        using DynResponse = http::response<http::dynamic_body>;

        auto [hostOrPath, port] = client.getEndpoint();
        Request req = client.buildRequest();

        boost::system::error_code lastEc{};
        int maxAttempts = std::max(1, client.retryPolicy.maxTries);

        for (int attempt = 0; attempt < maxAttempts; ++attempt)
        {
            auto [acqEc,
                  lease] = co_await client.pool_->acquire(hostOrPath, port);
            if (acqEc)
            {
                lastEc = acqEc;
                LOG_INFO("BufferExecutor: retrying ({}/{}) connection to {}",
                         attempt + 1, maxAttempts, hostOrPath);
                continue;
            }

            boost::system::error_code sendEc =
                co_await lease.get().send_request(req);
            if (sendEc)
            {
                lease.markInvalid();
                lastEc = sendEc;
                LOG_INFO("BufferExecutor: send failed, retrying ({}/{}) to {}",
                         attempt + 1, maxAttempts, hostOrPath);
                continue;
            }

            auto [recvEc, res] =
                co_await lease.get()
                    .template receive_response_as<http::dynamic_body>();
            if (recvEc)
            {
                lease.markInvalid();
                lastEc = recvEc;
                LOG_INFO("BufferExecutor: recv failed, retrying ({}/{}) to {}",
                         attempt + 1, maxAttempts, hostOrPath);
                continue;
            }

            if (!res.keep_alive() || !client.request.keepAlive)
            {
                lease.markInvalid();
            }

            co_return std::make_pair(recvEc, std::move(res));
        }

        co_return std::make_pair(lastEc, DynResponse{});
    }
};

// ---------------------------------------------------------------------------
// Out-of-line definition of WebClient::as<BodyTag>(args...)
// Must be after the executor types are defined.
// ---------------------------------------------------------------------------
template <typename Stream>
template <typename BodyTag, typename... Args>
auto WebClient<Stream>::as(Args&&... args)
{
    if constexpr (std::is_same_v<BodyTag, FileBodyTag>)
    {
        static_assert(sizeof...(Args) == 1,
                      "as<FileBodyTag> requires exactly one path argument");
        return FileExecutor<Stream>{
            *this, std::filesystem::path(std::forward<Args>(args)...)};
    }
    else if constexpr (std::is_same_v<BodyTag, BufferBodyTag>)
    {
        static_assert(sizeof...(Args) == 0,
                      "as<BufferBodyTag> takes no arguments");
        return BufferExecutor<Stream>{*this};
    }
    else
    {
        // StringBodyTag or default — delegates to the existing execute().
        return StringExecutor<Stream>{*this};
    }
}

} // namespace NSNAME
