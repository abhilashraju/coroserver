#pragma once

#include "logger.hpp"
#include "name_space.hpp"
#include "webclient.hpp"

#include <nlohmann/json.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <chrono>
#include <expected>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace NSNAME;

// ---------------------------------------------------------------------------
// Configuration types
// ---------------------------------------------------------------------------

/// One entry from the "subscriptions" array in the JSON config file.
struct SubscriptionConfig
{
    std::string name;
    /// DBus object path, e.g. /xyz/openbmc_project/Satellite/Systems/1
    std::string dbusPath;
    /// DBus interface, e.g. xyz.openbmc_project.Satellite.ComputerSystem
    std::string dbusInterface;
    /// Full GraphQL subscription string for GET /graphql/subscribe?query=...
    std::string query;
    /// Forwarded as ?interval= to the SSE endpoint.
    int intervalSeconds{10};
    /// True if the subscription returns an array of objects (one DBus object
    /// per element). False (default) for a single scalar object.
    bool isList{false};
    /// Dot-path in payload → DBus property name.  "status.health" → "StatusHealth"
    std::map<std::string, std::string> fieldMap;
};

struct BridgeConfig
{
    std::string host{"localhost"};
    std::string port{"8444"};
    std::vector<SubscriptionConfig> subscriptions;
};

// ---------------------------------------------------------------------------
// Config loader — returns std::expected, never throws.
// ---------------------------------------------------------------------------

inline std::expected<BridgeConfig, std::string>
    loadBridgeConfig(const std::string& path)
{
    std::ifstream f(path);
    if (!f)
        return std::unexpected("Cannot open config: " + path);

    auto doc = nlohmann::json::parse(f, nullptr, /*exceptions=*/false);
    if (doc.is_discarded())
        return std::unexpected("JSON parse error in: " + path);

    BridgeConfig cfg;
    cfg.host = doc.value("host", "localhost");
    cfg.port = doc.value("port", "8444");

    for (const auto& s : doc.value("subscriptions", nlohmann::json::array()))
    {
        SubscriptionConfig sc;
        sc.name            = s.value("name", "");
        sc.dbusPath        = s.value("dbus_path",
                                     "/xyz/openbmc_project/Satellite/Unknown");
        sc.dbusInterface   = s.value("dbus_interface",
                                     "xyz.openbmc_project.Satellite.Unknown");
        sc.query           = s.value("query", "");
        sc.intervalSeconds = s.value("interval_seconds", 10);
        sc.isList          = s.value("list", false);

        if (s.contains("field_map") && s["field_map"].is_object())
        {
            for (const auto& [k, v] : s["field_map"].items())
                sc.fieldMap[k] = v.get<std::string>();
        }

        if (!sc.query.empty())
            cfg.subscriptions.push_back(std::move(sc));
    }
    return cfg;
}

// ---------------------------------------------------------------------------
// jsonAtPath — resolve a dot-separated key path inside a JSON value.
//   "status.health"          → json["status"]["health"]   (object traversal)
//   "ipv4Addresses.0.address"→ json["ipv4Addresses"][0]["address"]
//                              (numeric segment = array index)
// Returns std::nullopt when any segment is missing or out of range.
// ---------------------------------------------------------------------------

inline std::optional<std::reference_wrapper<const nlohmann::json>>
    jsonAtPath(const nlohmann::json& root, const std::string& dotPath)
{
    const nlohmann::json* cur = &root;
    std::string seg;

    auto step = [&]() -> bool {
        if (cur->is_array())
        {
            // Numeric segment → array index
            bool isNum = !seg.empty() &&
                         std::all_of(seg.begin(), seg.end(), ::isdigit);
            if (!isNum)
                return false;
            std::size_t idx = static_cast<std::size_t>(std::stoul(seg));
            if (idx >= cur->size())
                return false;
            cur = &(*cur)[idx];
        }
        else if (cur->is_object())
        {
            if (!cur->contains(seg))
                return false;
            cur = &(*cur)[seg];
        }
        else
        {
            return false;
        }
        seg.clear();
        return true;
    };

    for (char c : dotPath)
    {
        if (c == '.')
        {
            if (!step())
                return std::nullopt;
        }
        else
        {
            seg += c;
        }
    }
    if (!seg.empty() && !step())
        return std::nullopt;

    return std::cref(*cur);
}

// ---------------------------------------------------------------------------
// DbusObjectProxy — owns one sdbusplus interface under a given path.
// ---------------------------------------------------------------------------

class DbusObjectProxy
{
  public:
    // sdbusplus calls (add_interface, register_property, initialize) must NOT
    // be called from inside an asio coroutine — sd_bus flushes synchronously
    // and crashes the event loop. Always construct outside ioc.run() or via
    // a net::post handler that runs at the top of the event loop.
    DbusObjectProxy(sdbusplus::asio::object_server& server,
                    const std::string& path, const std::string& iface,
                    const std::map<std::string, std::string>& fieldMap) :
        server_(server), path_(path), ifaceName_(iface)
    {
        iface_ = server_.add_interface(path_, ifaceName_);
        for (const auto& [jsonKey, propName] : fieldMap)
        {
            iface_->register_property(propName, std::string{});
            known_[propName] = "";
        }
        iface_->initialize();
        LOG_INFO("DBus object registered: {}  iface={}", path_, ifaceName_);
    }
    ~DbusObjectProxy()
    {
        if (iface_)
            server_.remove_interface(iface_);
    }

    DbusObjectProxy(const DbusObjectProxy&)            = delete;
    DbusObjectProxy& operator=(const DbusObjectProxy&) = delete;
    DbusObjectProxy(DbusObjectProxy&&)                 = delete;
    DbusObjectProxy& operator=(DbusObjectProxy&&)      = delete;

    // Safe to call from a coroutine — set_property does not flush sd_bus.
    bool applyUpdate(const nlohmann::json& payload,
                     const std::map<std::string, std::string>& fieldMap)
    {
        if (!iface_)
            return false;

        for (const auto& [jsonPath, propName] : fieldMap)
        {
            auto maybeVal = jsonAtPath(payload, jsonPath);
            if (!maybeVal)
                continue;

            std::string strVal = toStr(maybeVal->get());
            auto it = known_.find(propName);
            if (it == known_.end())
                continue;

            if (it->second != strVal)
            {
                iface_->set_property(propName, strVal);
                it->second = strVal;
                LOG_INFO("  ~ {}.{} = {}", ifaceName_, propName, strVal);
            }
        }
        return true;
    }

  private:
    static std::string toStr(const nlohmann::json& v)
    {
        if (v.is_string())          return v.get<std::string>();
        if (v.is_number_integer())  return std::to_string(v.get<int64_t>());
        if (v.is_number_unsigned()) return std::to_string(v.get<uint64_t>());
        if (v.is_number_float())    return std::to_string(v.get<double>());
        if (v.is_boolean())         return v.get<bool>() ? "true" : "false";
        return v.dump();
    }

    sdbusplus::asio::object_server& server_;
    std::string path_;
    std::string ifaceName_;
    std::shared_ptr<sdbusplus::asio::dbus_interface> iface_;
    std::map<std::string, std::string> known_;
};

// ---------------------------------------------------------------------------
// DbusObjectRegistry
//
// Owns a map of path → DbusObjectProxy for dynamic (list) subscriptions.
// New objects are created via create() which must be called from outside
// the coroutine stack (e.g. via net::post).
// ---------------------------------------------------------------------------
class DbusObjectRegistry
{
  public:
    DbusObjectRegistry(sdbusplus::asio::object_server& server,
                       const std::string& ifaceName,
                       const std::map<std::string, std::string>& fieldMap) :
        server_(server), ifaceName_(ifaceName), fieldMap_(fieldMap)
    {}

    // Create a new proxy for path if it doesn't exist yet.
    // Must be called outside the coroutine (no sd_bus flush from coroutine).
    void create(const std::string& path)
    {
        if (objs_.count(path))
            return;
        objs_.emplace(path, std::make_unique<DbusObjectProxy>(
                                server_, path, ifaceName_, fieldMap_));
    }

    // Called from coroutine — safe (only set_property).
    void applyUpdate(const std::string& path, const nlohmann::json& payload)
    {
        auto it = objs_.find(path);
        if (it != objs_.end())
            it->second->applyUpdate(payload, fieldMap_);
    }

    bool has(const std::string& path) const
    {
        return objs_.count(path) > 0;
    }

  private:
    sdbusplus::asio::object_server& server_;
    std::string ifaceName_;
    const std::map<std::string, std::string>& fieldMap_;
    std::map<std::string, std::unique_ptr<DbusObjectProxy>> objs_;
};

// ---------------------------------------------------------------------------
// SseSubscriptionClient
//
// Uses TcpClient to open a persistent TLS connection to graphql_redfish_server:
//
//   GET /graphql/subscribe?query=<url-encoded>&interval=<secs> HTTP/1.1
//
// The server responds with a chunked SSE stream:
//
//   data: {"data":{"systemStatus":{...}}}\n\n
//
// Every coroutine returns [ec, value] — no exceptions propagate.
// On any transport error the reconnect loop logs and retries with back-off.
// ---------------------------------------------------------------------------

class SseSubscriptionClient
{
  public:
    SseSubscriptionClient(boost::asio::io_context& ioc,
                          boost::asio::ssl::context& sslCtx,
                          const std::string& host, const std::string& port,
                          const SubscriptionConfig& cfg,
                          DbusObjectProxy* scalarProxy,
                          DbusObjectRegistry* listRegistry) :
        ioc_(ioc), sslCtx_(sslCtx), host_(host), port_(port), cfg_(cfg),
        scalarProxy_(scalarProxy), listRegistry_(listRegistry)
    {}

    void start()
    {
        net::co_spawn(ioc_, reconnectLoop(), net::detached);
    }

  private:
    // -----------------------------------------------------------------------
    // Reconnect loop with exponential back-off.
    // -----------------------------------------------------------------------
    net::awaitable<void> reconnectLoop()
    {
        using namespace std::chrono_literals;
        auto exec = co_await net::this_coro::executor;
        net::steady_timer delay(exec);
        auto backoff = 5s;

        while (true)
        {
            auto ec = co_await runStream();
            if (ec)
            {
                LOG_ERROR("[{}] SSE error: {} — retrying in {}s", cfg_.name,
                          ec.message(),
                          std::chrono::duration_cast<std::chrono::seconds>(
                              backoff).count());
            }
            else
            {
                LOG_INFO("[{}] SSE stream closed cleanly, reconnecting…",
                         cfg_.name);
            }

            boost::system::error_code tec;
            delay.expires_after(backoff);
            co_await delay.async_wait(
                net::redirect_error(net::use_awaitable, tec));
            if (tec)
                co_return; // io_context shutting down

            backoff = std::min(backoff * 2, std::chrono::seconds(60));
        }
    }

    // -----------------------------------------------------------------------
    // Open one TLS connection, send the SSE GET, read frames until done.
    // Returns the first non-EOF error_code, or a default (success) on clean
    // server close.
    // -----------------------------------------------------------------------
    net::awaitable<boost::system::error_code> runStream()
    {
        WebClient<beast::tcp_stream> wc(ioc_, sslCtx_);
        wc.withHost(host_)
            .withPort(port_)
            .withTarget("/graphql/subscribe")
            .withParams({{"query",    urlEncode(cfg_.query)},
                         {"interval", std::to_string(cfg_.intervalSeconds)}})
            .withFrameDelimiter("\n\n");

        auto [ec, stream] = co_await wc.executeAsStream();
        if (ec)
        {
            LOG_ERROR("[{}] SSE connect failed: {}", cfg_.name, ec.message());
            co_return ec;
        }

        LOG_INFO("[{}] SSE stream open", cfg_.name);

        while (true)
        {
            auto frame = co_await stream->next();

            if (frame.ec == boost::asio::error::eof)
                co_return boost::system::error_code{}; // clean close

            if (frame.ec)
            {
                LOG_ERROR("[{}] read frame failed: {}", cfg_.name,
                          frame.ec.message());
                co_return frame.ec;
            }

            handleFrame(frame.data);
        }
    }

    // -----------------------------------------------------------------------
    // Parse one SSE frame — find "data: " lines and dispatch each.
    // -----------------------------------------------------------------------
    void handleFrame(const std::string& frame)
    {
        constexpr std::string_view kDataPrefix = "data: ";
        std::string_view sv(frame);

        while (!sv.empty())
        {
            auto nl = sv.find('\n');
            std::string_view line = sv.substr(0, nl);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);

            if (line.starts_with(kDataPrefix))
                dispatchEvent(std::string(line.substr(kDataPrefix.size())));

            if (nl == std::string_view::npos)
                break;
            sv = sv.substr(nl + 1);
        }
    }

    // -----------------------------------------------------------------------
    // Dispatch one SSE data value.
    // Returns std::expected<void, std::string> so the caller can see why
    // a payload was rejected without an exception being raised.
    // Shape: { "data": { "<field>": { ... } | [ ... ] } }
    // -----------------------------------------------------------------------
    std::expected<void, std::string>
        dispatchEvent(const std::string& rawJson)
    {
        auto event =
            nlohmann::json::parse(rawJson, nullptr, /*exceptions=*/false);
        if (event.is_discarded())
        {
            LOG_WARNING("[{}] Unparseable SSE payload ignored", cfg_.name);
            return std::unexpected("json parse failed");
        }

        if (event.contains("errors"))
        {
            std::string msg = event["errors"].dump();
            LOG_ERROR("[{}] GraphQL errors: {}", cfg_.name, msg);
            return std::unexpected("graphql error: " + msg);
        }

        if (!event.contains("data") || !event["data"].is_object())
        {
            LOG_WARNING("[{}] Unexpected event shape: {}", cfg_.name, rawJson);
            return std::unexpected("unexpected shape");
        }

        for (const auto& [fieldName, payload] : event["data"].items())
        {
            LOG_INFO("[{}] Received event for '{}'", cfg_.name, fieldName);

            if (payload.is_null() || payload.is_discarded())
            {
                LOG_INFO("[{}]   payload is null — skipping", cfg_.name);
                continue;
            }

            if (payload.is_array() && listRegistry_)
            {
                for (const auto& item : payload)
                {
                    std::string itemPath = cfg_.dbusPath;
                    if (item.contains("id") && item["id"].is_string())
                        itemPath += "/" + item["id"].get<std::string>();

                    if (!listRegistry_->has(itemPath))
                    {
                        // Create new DBus object outside the coroutine stack.
                        auto path = itemPath;
                        net::post(ioc_, [this, path]() {
                            listRegistry_->create(path);
                        });
                    }
                    else
                    {
                        listRegistry_->applyUpdate(itemPath, item);
                    }
                }
            }
            else if (payload.is_object() && scalarProxy_)
            {
                scalarProxy_->applyUpdate(payload, cfg_.fieldMap);
            }
            else
            {
                LOG_INFO("[{}]   unexpected payload type — skipping",
                         cfg_.name);
            }
        }
        return {};
    }

    // -----------------------------------------------------------------------
    // Minimal percent-encoder for a query-string value.
    // -----------------------------------------------------------------------
    static std::string urlEncode(const std::string& s)
    {
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size() * 3);
        for (unsigned char c : s)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                out += static_cast<char>(c);
            else
            {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 0x0F];
            }
        }
        return out;
    }

    boost::asio::io_context& ioc_;
    boost::asio::ssl::context& sslCtx_;
    std::string host_;
    std::string port_;
    const SubscriptionConfig& cfg_;
    DbusObjectProxy*     scalarProxy_{nullptr};  // for scalar subscriptions
    DbusObjectRegistry*  listRegistry_{nullptr}; // for list subscriptions
};

// ---------------------------------------------------------------------------
// GraphqlDbusBridge
// ---------------------------------------------------------------------------

class GraphqlDbusBridge
{
  public:
    // Returns unique_ptr — GraphqlDbusBridge must never be moved after
    // construction because DbusObjectProxy members hold sdbusplus::asio::
    // object_server& (a reference into this object). A move would change
    // objServer_'s address and dangle all those references.
    static std::expected<std::unique_ptr<GraphqlDbusBridge>, std::string>
        create(boost::asio::io_context& ioc,
               std::shared_ptr<sdbusplus::asio::connection> conn,
               const std::string& configPath)
    {
        auto maybeCfg = loadBridgeConfig(configPath);
        if (!maybeCfg)
            return std::unexpected(maybeCfg.error());

        return std::make_unique<GraphqlDbusBridge>(
            ioc, std::move(conn), std::move(*maybeCfg));
    }

    void start()
    {
        for (auto& c : clients_)
            c->start();
        LOG_INFO("GraphqlDbusBridge started ({} SSE streams)", clients_.size());
    }

    GraphqlDbusBridge(boost::asio::io_context& ioc,
                      std::shared_ptr<sdbusplus::asio::connection> conn,
                      BridgeConfig cfg) :
        ioc_(ioc), objServer_(conn), cfg_(std::move(cfg))
    {
        LOG_INFO("Bridge: {} subscription(s)", cfg_.subscriptions.size());

        sslCtx_ = std::make_unique<boost::asio::ssl::context>(
            boost::asio::ssl::context::tlsv12_client);
        sslCtx_->set_default_verify_paths();
        sslCtx_->set_verify_mode(boost::asio::ssl::verify_none);

        for (const auto& sub : cfg_.subscriptions)
        {
            // Scalar proxies are pre-created here (before ioc.run()) so that
            // sdbusplus initialize() is safe. List registries are created here
            // too; individual per-item proxies are created lazily via net::post
            // when new item ids arrive in SSE events.
            bool isList = sub.isList;

            DbusObjectProxy*    scalarProxy  = nullptr;
            DbusObjectRegistry* listRegistry = nullptr;

            if (isList)
            {
                registries_.push_back(std::make_unique<DbusObjectRegistry>(
                    objServer_, sub.dbusInterface, sub.fieldMap));
                listRegistry = registries_.back().get();
            }
            else
            {
                // Scalar — pre-create before ioc.run() so initialize() is safe.
                proxies_.push_back(std::make_unique<DbusObjectProxy>(
                    objServer_, sub.dbusPath, sub.dbusInterface, sub.fieldMap));
                scalarProxy = proxies_.back().get();
            }

            clients_.push_back(std::make_unique<SseSubscriptionClient>(
                ioc_, *sslCtx_, cfg_.host, cfg_.port, sub,
                scalarProxy, listRegistry));
        }
    }

    boost::asio::io_context& ioc_;
    sdbusplus::asio::object_server objServer_;
    BridgeConfig cfg_;
    std::unique_ptr<boost::asio::ssl::context> sslCtx_;
    std::vector<std::unique_ptr<DbusObjectProxy>>    proxies_;
    std::vector<std::unique_ptr<DbusObjectRegistry>> registries_;
    std::vector<std::unique_ptr<SseSubscriptionClient>> clients_;
};
