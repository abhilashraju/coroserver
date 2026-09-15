#pragma once
#include "beastdefs.hpp"
#include "http_client.hpp"
#include "logger.hpp"
#include "make_awaitable.hpp"

#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace NSNAME
{

/**
 * @brief Configuration options for ConnectionPool
 */
struct ConnectionPoolConfig
{
    // Maximum active + idle connections per endpoint key (host:port or unix
    // path)
    std::size_t maxConnectionsPerHost{5};

    // Idle connections older than this are closed and discarded instead of
    // reused
    std::chrono::seconds idleTimeout{30};

    // Timeout when waiting in queue to acquire an available connection
    std::chrono::seconds acquireTimeout{10};
};

/**
 * @brief Represents an endpoint identifier for connection pooling.
 */
struct EndpointKey
{
    std::string hostOrPath;
    std::string port; // empty for unix domain sockets

    bool operator==(const EndpointKey& other) const
    {
        return hostOrPath == other.hostOrPath && port == other.port;
    }

    bool operator<(const EndpointKey& other) const
    {
        return std::tie(hostOrPath, port) <
               std::tie(other.hostOrPath, other.port);
    }
};

} // namespace NSNAME

namespace std
{
template <>
struct hash<NSNAME::EndpointKey>
{
    std::size_t operator()(const NSNAME::EndpointKey& k) const noexcept
    {
        std::size_t h1 = std::hash<std::string>{}(k.hostOrPath);
        std::size_t h2 = std::hash<std::string>{}(k.port);
        return h1 ^ (h2 << 1);
    }
};
} // namespace std

namespace NSNAME
{

template <typename Stream>
class ConnectionPool;

/**
 * @brief RAII Lease for a pooled HttpClient connection.
 * When destroyed or released, returns the connection to the pool or drops it if
 * marked dirty/closed.
 */
template <typename Stream>
class PooledConnection
{
  public:
    PooledConnection() = default;

    PooledConnection(std::shared_ptr<ConnectionPool<Stream>> pool,
                     EndpointKey key,
                     std::unique_ptr<HttpClient<Stream>> client) :
        pool_(std::move(pool)), key_(std::move(key)),
        client_(std::move(client)), valid_(true)
    {}

    ~PooledConnection()
    {
        release();
    }

    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;

    PooledConnection(PooledConnection&& other) noexcept :
        pool_(std::move(other.pool_)), key_(std::move(other.key_)),
        client_(std::move(other.client_)), valid_(other.valid_)
    {
        other.valid_ = false;
    }

    PooledConnection& operator=(PooledConnection&& other) noexcept
    {
        if (this != &other)
        {
            release();
            pool_ = std::move(other.pool_);
            key_ = std::move(other.key_);
            client_ = std::move(other.client_);
            valid_ = other.valid_;
            other.valid_ = false;
        }
        return *this;
    }

    HttpClient<Stream>& get()
    {
        return *client_;
    }

    const HttpClient<Stream>& get() const
    {
        return *client_;
    }

    HttpClient<Stream>* operator->()
    {
        return client_.get();
    }

    const HttpClient<Stream>* operator->() const
    {
        return client_.get();
    }

    HttpClient<Stream>& operator*()
    {
        return *client_;
    }

    const HttpClient<Stream>& operator*() const
    {
        return *client_;
    }

    bool isValid() const
    {
        return valid_ && client_ != nullptr;
    }

    explicit operator bool() const
    {
        return isValid();
    }

    /// Mark the connection as invalid (e.g. on socket error or HTTP Connection:
    /// close) so it will be destroyed instead of returned to the pool.
    void markInvalid()
    {
        valid_ = false;
    }

    /// Explicitly release and transfer ownership of the client without
    /// returning it to the pool
    std::unique_ptr<HttpClient<Stream>> releaseClient()
    {
        valid_ = false;
        if (pool_)
        {
            pool_->returnConnection(key_, nullptr, false);
            pool_.reset();
        }
        return std::move(client_);
    }

    /// Explicitly return the connection back to the pool immediately.
    void release();

  private:
    std::shared_ptr<ConnectionPool<Stream>> pool_{nullptr};
    EndpointKey key_{};
    std::unique_ptr<HttpClient<Stream>> client_{nullptr};
    bool valid_{false};
};

/**
 * @brief Async Connection Pool for HTTP / HTTPS / Unix domain socket clients.
 *
 * Provides connection reuse (HTTP Keep-Alive), per-host connection limits,
 * idle connection expiration, and coroutine-aware waiting queues.
 */
template <typename Stream>
class ConnectionPool :
    public std::enable_shared_from_this<ConnectionPool<Stream>>
{
  public:
    using Lease = PooledConnection<Stream>;

    struct IdleEntry
    {
        std::unique_ptr<HttpClient<Stream>> client;
        std::chrono::steady_clock::time_point lastUsed;
    };

    struct HostState
    {
        std::size_t activeCount{0};
        std::vector<IdleEntry> idle;
        // Waiters waiting for a connection to become available
        std::deque<std::shared_ptr<net::steady_timer>> waiters;
    };

    ConnectionPool(net::io_context& ioc, ssl::context& sslCtx,
                   ConnectionPoolConfig config = {}) :
        ioc_(ioc), sslCtx_(sslCtx), config_(config)
    {}

    ~ConnectionPool()
    {
        clear();
    }

    static std::shared_ptr<ConnectionPool<Stream>> create(
        net::io_context& ioc, ssl::context& sslCtx,
        ConnectionPoolConfig config = {})
    {
        return std::make_shared<ConnectionPool<Stream>>(ioc, sslCtx, config);
    }

    net::io_context& getIoContext()
    {
        return ioc_;
    }

    ssl::context& getSslContext()
    {
        return sslCtx_;
    }

    auto getExecutor() -> net::io_context::executor_type
    {
        return ioc_.get_executor();
    }

    const ConnectionPoolConfig& getConfig() const
    {
        return config_;
    }

    /**
     * @brief Asynchronously acquires a connection for the given endpoint.
     * Reuses an idle keep-alive connection if available; otherwise creates and
     * connects a new one (or suspends until one becomes available if
     * maxConnectionsPerHost is reached).
     */
    net::awaitable<std::pair<boost::system::error_code, Lease>> acquire(
        const std::string& hostOrPath, const std::string& port = "")
    {
        EndpointKey key{hostOrPath, port};
        auto self = this->shared_from_this();
        auto now = std::chrono::steady_clock::now();

        while (true)
        {
            auto& state = hosts_[key];

            // 1. Try to find a valid idle connection
            if (auto client = extractIdleConnection(state, now))
            {
                state.activeCount++;
                co_return std::make_pair(boost::system::error_code{},
                                         Lease(self, key, std::move(client)));
            }

            // 2. Check if we can create a new connection
            if (state.activeCount < config_.maxConnectionsPerHost)
            {
                auto [ec, client] =
                    co_await createNewConnection(key, hostOrPath, port);
                if (ec)
                {
                    co_return std::make_pair(ec, Lease{});
                }
                co_return std::make_pair(boost::system::error_code{},
                                         Lease(self, key, std::move(client)));
            }

            // 3. Reached max capacity: suspend and wait for an active
            // connection to be released
            boost::system::error_code waitEc =
                co_await waitForAvailableSlot(key);
            if (waitEc)
            {
                co_return std::make_pair(waitEc, Lease{});
            }
            // Loop back to try acquiring either the returned idle connection or
            // a freed slot
        }
    }

    /**
     * @brief Called by PooledConnection to return or destroy a connection.
     */
    void returnConnection(const EndpointKey& key,
                          std::unique_ptr<HttpClient<Stream>> client,
                          bool valid)
    {
        auto it = hosts_.find(key);
        if (it == hosts_.end())
        {
            return;
        }

        auto& state = it->second;
        if (state.activeCount > 0)
        {
            state.activeCount--;
        }

        if (valid && client)
        {
            state.idle.push_back(
                IdleEntry{std::move(client), std::chrono::steady_clock::now()});
        }

        notifyWaiter(key);
    }

    /**
     * @brief Clears all idle connections and waiters across all hosts.
     */
    void clear()
    {
        for (auto& [key, state] : hosts_)
        {
            for (auto& w : state.waiters)
            {
                w->cancel();
            }
            state.waiters.clear();
            state.idle.clear();
            state.activeCount = 0;
        }
        hosts_.clear();
    }

    /**
     * @brief Returns the total count of active (leased) connections.
     */
    std::size_t activeCount(const EndpointKey& key) const
    {
        auto it = hosts_.find(key);
        if (it != hosts_.end())
        {
            return it->second.activeCount;
        }
        return 0;
    }

    /**
     * @brief Returns the total count of idle pooled connections for a host.
     */
    std::size_t idleCount(const EndpointKey& key) const
    {
        auto it = hosts_.find(key);
        if (it != hosts_.end())
        {
            return it->second.idle.size();
        }
        return 0;
    }

  private:
    std::unique_ptr<HttpClient<Stream>> extractIdleConnection(
        HostState& state, std::chrono::steady_clock::time_point now)
    {
        while (!state.idle.empty())
        {
            auto entry = std::move(state.idle.back());
            state.idle.pop_back();

            if (now - entry.lastUsed <= config_.idleTimeout)
            {
                return std::move(entry.client);
            }
            // Expired idle connection is dropped and destroyed here
        }
        return nullptr;
    }

    net::awaitable<std::pair<boost::system::error_code,
                             std::unique_ptr<HttpClient<Stream>>>>
        createNewConnection(const EndpointKey& key,
                            const std::string& hostOrPath,
                            const std::string& port)
    {
        hosts_[key].activeCount++;
        auto client = std::make_unique<HttpClient<Stream>>(ioc_, sslCtx_);
        boost::system::error_code ec =
            co_await client->connect(hostOrPath, port);
        if (ec)
        {
            auto& state = hosts_[key];
            if (state.activeCount > 0)
            {
                state.activeCount--;
            }
            notifyWaiter(key);
            co_return std::make_pair(ec, nullptr);
        }
        co_return std::make_pair(boost::system::error_code{},
                                 std::move(client));
    }

    void notifyWaiter(const EndpointKey& key)
    {
        auto it = hosts_.find(key);
        if (it != hosts_.end() && !it->second.waiters.empty())
        {
            auto timer = it->second.waiters.front();
            it->second.waiters.pop_front();
            timer->cancel();
        }
    }

    net::awaitable<boost::system::error_code> waitForAvailableSlot(
        const EndpointKey& key)
    {
        auto timer = std::make_shared<net::steady_timer>(
            co_await net::this_coro::executor);
        timer->expires_after(config_.acquireTimeout);
        hosts_[key].waiters.push_back(timer);

        boost::system::error_code waitEc;
        co_await timer->async_wait(
            net::redirect_error(net::use_awaitable, waitEc));

        if (waitEc != boost::asio::error::operation_aborted)
        {
            std::erase(hosts_[key].waiters, timer);
            co_return boost::system::errc::make_error_code(
                boost::system::errc::timed_out);
        }
        co_return boost::system::error_code{};
    }

    net::io_context& ioc_;
    ssl::context& sslCtx_;
    ConnectionPoolConfig config_;
    std::unordered_map<EndpointKey, HostState> hosts_;
};

template <typename Stream>
void PooledConnection<Stream>::release()
{
    if (pool_ && client_)
    {
        pool_->returnConnection(key_, std::move(client_), valid_);
        pool_.reset();
    }
    client_.reset();
    valid_ = false;
}

} // namespace NSNAME
