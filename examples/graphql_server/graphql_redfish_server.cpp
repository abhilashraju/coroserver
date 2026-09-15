#include "command_line_parser.hpp"
#include "graphql_redfish_executor.hpp"
#include "graphql_redfish_provider.hpp"
#include "graphql_redfish_schema.hpp"
#include "http_server.hpp"
#include "logger.hpp"
#include "ql_dbus_property_watcher.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <string>

using namespace NSNAME;

namespace
{

constexpr int kIntervalMinSecs = 5;
constexpr int kIntervalMaxSecs = 300;
constexpr int kIntervalDefaultSecs = 5;

// Parses a string as a decimal integer without throwing.
// Accepts an optional leading '-' or '+'.
// Returns std::unexpected with a contextual error message on failure.
std::expected<int, std::string> parseIntString(const std::string& str,
                                               const std::string& paramName)
{
    if (str.empty())
    {
        return std::unexpected(
            "Invalid '" + paramName + "': value must not be empty");
    }

    const char* p = str.c_str();
    if (*p == '-' || *p == '+')
    {
        ++p;
    }
    if (*p == '\0')
    {
        return std::unexpected(
            "Invalid '" + paramName + "': must be an integer");
    }
    for (; *p != '\0'; ++p)
    {
        if (*p < '0' || *p > '9')
        {
            return std::unexpected(
                "Invalid '" + paramName + "': must be an integer");
        }
    }

    // All characters are valid digits — stoi will not throw.
    return std::stoi(str);
}

// Returns the clamped poll interval [kIntervalMinSecs, kIntervalMaxSecs] on
// success, or an error message on failure.
std::expected<int, std::string> parseInterval(const std::string& intervalStr)
{
    if (intervalStr.empty())
    {
        return kIntervalDefaultSecs;
    }

    return parseIntString(intervalStr, "interval").transform([](int v) {
        return std::clamp(v, kIntervalMinSecs, kIntervalMaxSecs);
    });
}

// Returns the server port as an integer on success, or an error message on
// failure.
std::expected<int, std::string> parsePort(const std::string& portStr)
{
    return parseIntString(portStr, "port");
}

} // namespace

// Returns std::unexpected(errorMessage) on any configuration/setup failure,
// or void on success (the io_context.run() is called within this function).
std::expected<void, std::string> run(int argc, const char* argv[])
{
    auto [cert, port, host, targetPort, user, password, clientCert, clientKey] =
        getArgs(parseCommandline(argc, argv), "--cert,-c", "--port,-p",
                "--host,-h", "--target-port,-t", "--user,-u", "--password,-w",
                "--client-cert,-C", "--client-key,-K");

    // Require at least one authentication method.
    bool hasBasicAuth = user.has_value() && password.has_value();
    bool hasMtls = clientCert.has_value() && clientKey.has_value();
    if (!hasBasicAuth && !hasMtls)
    {
        return std::unexpected(
            "Authentication required: provide either "
            "--user/-u and --password/-w (basic auth) "
            "or --client-cert/-C and --client-key/-K (mTLS)");
    }

    boost::asio::io_context ioContext;

    RedfishProviderConfig providerConfig;
    providerConfig.host = host ? std::string(*host) : "localhost";
    providerConfig.port = targetPort ? std::string(*targetPort) : "443";
    if (hasBasicAuth)
    {
        providerConfig.username = std::string(*user);
        providerConfig.password = std::string(*password);
    }
    if (hasMtls)
    {
        providerConfig.clientCertFile = std::string(*clientCert);
        providerConfig.clientKeyFile = std::string(*clientKey);
    }

    auto provider =
        std::make_shared<HttpRedfishProvider>(ioContext, providerConfig);

    // Build the schema once — it is pure static data (GraphQL type definitions)
    // independent of which Redfish host is queried.  Keep it in a shared_ptr so
    // resolveExecutor can copy it cheaply for per-remoteIp executors without
    // re-reading the file from disk.
    auto schemaResult = buildRedfishTypedSchema();
    if (!schemaResult)
    {
        return std::unexpected(
            "Failed to build schema: " + schemaResult.error());
    }
    auto schema =
        std::make_shared<graphql::TypedSchema>(std::move(*schemaResult));

    auto executor = std::make_shared<RedfishGraphQLExecutor>(*schema, provider);

    // Returns the default executor when remoteIp is empty or matches the
    // configured default host. Otherwise creates a fresh provider+executor
    // targeting that host (same port and auth). The schema is copied from the
    // already-parsed shared_ptr — no file I/O per request.
    // Cache one executor per remoteIp so the authentication token (sharedToken
    // inside HttpRedfishProvider) is preserved across SSE reconnects.
    // Without this, every reconnect creates a brand-new unauthenticated
    // provider that has to re-authenticate from scratch, which can fail with
    // permission_denied when the BMC session limit is reached.
    std::unordered_map<std::string, std::shared_ptr<RedfishGraphQLExecutor>>
        remoteExecutors;

    auto resolveExecutor = [&ioContext, &providerConfig, &executor, schema,
                            &remoteExecutors](const std::string& remoteIp)
        -> std::shared_ptr<RedfishGraphQLExecutor> {
        if (remoteIp.empty() || remoteIp == providerConfig.host)
        {
            return executor;
        }
        auto it = remoteExecutors.find(remoteIp);
        if (it != remoteExecutors.end())
        {
            return it->second;
        }
        RedfishProviderConfig remoteConfig = providerConfig;
        remoteConfig.host = remoteIp;
        auto remoteExec = std::make_shared<RedfishGraphQLExecutor>(
            *schema,
            std::make_shared<HttpRedfishProvider>(ioContext, remoteConfig));
        remoteExecutors.emplace(remoteIp, remoteExec);
        return remoteExec;
    };

    boost::asio::ssl::context sslContext(boost::asio::ssl::context::sslv23);
    sslContext.set_options(boost::asio::ssl::context::default_workarounds |
                           boost::asio::ssl::context::no_sslv2 |
                           boost::asio::ssl::context::single_dh_use);

    // bmcweb stores both the certificate chain and the private key in one
    // combined PEM file at /etc/ssl/certs/https/server.pem.
    // When --cert is supplied it is treated as a directory that follows the
    // same single-file convention (server.pem holds both).
    std::string pemFile = cert ? std::string(*cert) + "/server.pem"
                               : "/etc/ssl/certs/https/server.pem";

    // Load certificate and private key using the error_code overload to avoid
    // exceptions.
    boost::system::error_code ec;
    sslContext.use_certificate_chain_file(pemFile, ec);
    if (ec)
    {
        return std::unexpected("Failed to load certificate chain from '" +
                               pemFile + "': " + ec.message());
    }
    sslContext.use_private_key_file(pemFile, boost::asio::ssl::context::pem,
                                    ec);
    if (ec)
    {
        return std::unexpected("Failed to load private key from '" + pemFile +
                               "': " + ec.message());
    }

    // Parse the server port without throwing.
    int serverPort = 8444;
    if (port)
    {
        auto portResult = parsePort(std::string(*port));
        if (!portResult)
        {
            return std::unexpected(portResult.error());
        }
        serverPort = *portResult;
    }

    HttpRouter router;
    router.setIoContext(ioContext);

    // POST /graphql?remoteIp=<host>  (remoteIp is optional)
    router.add_post_handler(
        "/graphql",
        [resolveExecutor](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            nlohmann::json requestBody =
                nlohmann::json::parse(req.body(), nullptr, false);

            if (requestBody.is_discarded())
            {
                co_return make_bad_request_error("Invalid JSON in request body",
                                                 req.version());
            }

            if (!requestBody.contains("query"))
            {
                co_return make_bad_request_error(
                    "Missing 'query' field in request", req.version());
            }

            std::string query = requestBody["query"].get<std::string>();
            nlohmann::json variables = requestBody.contains("variables")
                                           ? requestBody["variables"]
                                           : nlohmann::json::object();

            // Keep the shared_ptr alive for the entire co_await chain.
            // Without this named local the temporary is destroyed at the
            // first suspension point, leaving execute() running on a
            // freed object (use-after-free → SIGSEGV).
            auto exec = resolveExecutor(params["remoteIp"]);
            nlohmann::json response = co_await exec->execute(query, variables);
            co_return make_success_response(response, http::status::ok,
                                            req.version());
        });

    router.add_get_handler(
        "/health", [](Request& req, const http_function& params) -> Response {
            nlohmann::json response = {{"status", "healthy"},
                                       {"service", "Redfish GraphQL Server"}};
            return make_success_response(response, http::status::ok,
                                         req.version());
        });

    router.add_get_handler(
        "/schema", [](Request& req, const http_function& params) -> Response {
            nlohmann::json schemaDoc = {
                {"queries",
                 {{"serviceRoot", "Get Redfish service root"},
                  {"systems", "List Redfish systems"},
                  {"system", "Get a Redfish system by id"},
                  {"chassis", "List Redfish chassis"},
                  {"managers", "List Redfish managers"}}},
                {"subscriptions",
                 {{"systemStatus",
                   "Stream live updates for a ComputerSystem (arg: id)"},
                  {"chassisStatus",
                   "Stream live updates for a Chassis (arg: id)"}}},
                {"events",
                 {{"POST /graphql/events",
                   "Inject an event to fire matching subscriptions immediately. "
                   "Body: {\"fields\":[\"fieldName\",...]}"}}},
                {"triggerModes",
                 {{"event",
                   "DBus-driven or externally injected — wakes on signal (default)"},
                  {"timer",
                   "Poll on fixed interval (use &interval=N, default 5s)"}}}};
            return make_success_response(schemaDoc, http::status::ok,
                                         req.version());
        });

    router.add_get_handler(
        "/graphql/subscriptions/stats",
        [executor](Request& req, const http_function& params) -> Response {
            nlohmann::json stats = executor->getSubscriptionStats();
            return make_success_response(stats, http::status::ok,
                                         req.version());
        });

    // External event injection endpoint.
    // POST /graphql/events
    // Body: { "fields": ["fieldName1", "fieldName2", ...] }
    //
    // Fires all active event-triggered subscription sessions whose query
    // references at least one of the named fields. This lets any external
    // process (e.g. a BMC daemon, a test harness, or a Redfish event hook)
    // push an immediate update without waiting for the next DBus signal.
    //
    // Example:
    //   curl -sk -X POST https://localhost:8444/graphql/events
    //        -H 'Content-Type: application/json'
    //        -d '{"fields":["chassisStatus","systemStatus"]}'
    router.add_post_handler(
        "/graphql/events",
        [executor](Request& req,
                   const http_function& params) -> net::awaitable<Response> {
            nlohmann::json body =
                nlohmann::json::parse(req.body(), nullptr, false);

            if (body.is_discarded() || !body.contains("fields") ||
                !body["fields"].is_array())
            {
                co_return make_bad_request_error(
                    "Request body must be JSON with a 'fields' array",
                    req.version());
            }

            std::unordered_set<std::string> fields;
            for (const auto& item : body["fields"])
            {
                if (item.is_string())
                {
                    fields.insert(item.get<std::string>());
                }
            }

            if (fields.empty())
            {
                co_return make_bad_request_error(
                    "'fields' array must contain at least one string",
                    req.version());
            }

            executor->notifyFieldsChanged(fields);

            nlohmann::json response = {{"fired", true},
                                       {"fields", nlohmann::json(fields)}};
            co_return make_success_response(response, http::status::ok,
                                            req.version());
        });

    // GET /graphql/subscribe?query=...&remoteIp=<host>
    // Optional params: &interval=5  &trigger=event  &remoteIp=<host>
    router.add_sse_handler(
        "/graphql/subscribe",
        [resolveExecutor](Request& req, const http_function& params,
                          SseWriter writer) -> net::awaitable<void> {
            // parse_function already split and URL-decoded the query string
            std::string query = params["query"];
            std::string triggerParam =
                params["trigger"]; // "timer" (default) or "event"

            if (query.empty())
            {
                nlohmann::json err = {
                    {"errors",
                     {{{"message",
                        "Missing 'query' query-string parameter"}}}}};
                co_await writer.write(err.dump());
                co_return;
            }

            auto exec = co_await net::this_coro::executor;

            // Optional remoteIp: route Redfish calls to the specified host.
            auto activeExecutor = resolveExecutor(params["remoteIp"]);

            // Build the appropriate trigger based on the 'trigger' parameter.
            std::shared_ptr<graphql::SubscriptionTrigger> trigger;
            if (triggerParam == "event")
            {
                trigger = std::make_shared<graphql::EventTrigger>(exec);
            }
            else
            {
                std::string intervalStr = params["interval"];
                std::expected<int, std::string> maybeInterval =
                    parseInterval(intervalStr);
                if (!maybeInterval)
                {
                    nlohmann::json err = {
                        {"errors", {{{"message", maybeInterval.error()}}}}};
                    co_await writer.write(err.dump());
                    co_return;
                }
                trigger = std::make_shared<graphql::TimerTrigger>(
                    exec, std::chrono::seconds(*maybeInterval));
            }

            co_await activeExecutor->executeSubscription(
                query, nlohmann::json::object(), std::move(trigger),
                [&writer](nlohmann::json event) -> net::awaitable<bool> {
                    bool ok = co_await writer.write(event.dump());
                    co_return ok;
                });
        });

    TcpStreamType acceptor(ioContext.get_executor(), serverPort, sslContext);
    HttpServer server(ioContext, acceptor, router);

    // Register a single bus-wide PropertiesChanged watcher. It filters signals
    // through the DbusWatcherIndex from the schema and wakes only the sessions
    // whose queries reference the changed fields.
    auto conn = std::make_shared<sdbusplus::asio::connection>(ioContext);
    auto propertyWatcher = std::make_shared<GraphQLDbusPropertyWatcher>(
        conn, executor->getDbusWatchers(),
        [executor](const std::unordered_set<std::string>& fields) {
            executor->notifyFieldsChanged(fields);
        });

    LOG_INFO("Redfish GraphQL Server started on port {}", serverPort);
    LOG_INFO("Querying Redfish target {}:{}", providerConfig.host,
             providerConfig.port);

    ioContext.run();
    return {};
}

int main(int argc, const char* argv[])
{
    auto result = run(argc, argv);
    if (!result)
    {
        LOG_ERROR("{}", result.error());
        return 1;
    }
    return 0;
}
