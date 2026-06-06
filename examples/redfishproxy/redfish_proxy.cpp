#include "command_line_parser.hpp"
#include "http_server.hpp"
#include "logger.hpp"
#include "redfish_client.hpp"

#include <nlohmann/json.hpp>

#include <memory>

using namespace reactor;
using json = nlohmann::json;

// Global proxy state with RedfishClient
// RedfishClient creates fresh connections per request and handles token refresh
// automatically
struct ProxyState
{
    std::unique_ptr<ssl::context> sslContext;
    std::unique_ptr<RedfishClient> client;
    bool configured{false};

    void reset()
    {
        client.reset();
        sslContext.reset();
        configured = false;
    }
};

static ProxyState proxyState;

// Handler to configure the proxy server
net::awaitable<Response> handleConfigEndpoint(Request& req,
                                              const http_function& params)
{
    Response res{http::status::created, req.version()};
    res.set(http::field::content_type, "application/json");
    res.keep_alive(req.keep_alive());

    try
    {
        // Parse the JSON body
        json requestBody = json::parse(req.body());

        // Extract configuration parameters
        std::string host = requestBody.value("host", "");
        std::string port = requestBody.value("port", "443");
        std::string protocol = requestBody.value("protocol", "https");
        std::string user = requestBody.value("username", "");
        std::string pass = requestBody.value("password", "");

        if (host.empty() || user.empty() || pass.empty())
        {
            res.result(http::status::bad_request);
            json errorResponse = {
                {"error", "Missing required fields: host, username, password"}};
            res.body() = errorResponse.dump();
            res.prepare_payload();
            co_return res;
        }

        // Reset previous configuration
        proxyState.reset();

        // Get io_context from current coroutine
        auto executor = co_await net::this_coro::executor;
        net::io_context& ioc =
            static_cast<net::io_context&>(executor.context());

        // Create SSL context
        proxyState.sslContext =
            std::make_unique<ssl::context>(ssl::context::tlsv12_client);
        proxyState.sslContext->set_default_verify_paths();
        proxyState.sslContext->set_verify_mode(ssl::verify_none);

        // Create Redfish client
        proxyState.client =
            std::make_unique<RedfishClient>(ioc, *proxyState.sslContext);
        proxyState.client->withHost(host)
            .withPort(port)
            .withProtocol(protocol)
            .withUserName(user)
            .withPassword(pass);

        // Test authentication by getting a token
        auto [tokenEc, token] = co_await proxyState.client->getToken();
        if (tokenEc)
        {
            res.result(http::status::unauthorized);
            json errorResponse = {
                {"error", "Failed to authenticate with target server"},
                {"details", tokenEc.message()}};
            res.body() = errorResponse.dump();
            res.prepare_payload();
            LOG_ERROR("Authentication failed: {}", tokenEc.message());
            proxyState.reset();
            co_return res;
        }

        proxyState.configured = true;

        // Set X-Auth-Token header in response
        res.set("X-Auth-Token", token);

        json successResponse = {
            {"status", "configured"},
            {"target", protocol + "://" + host + ":" + port},
            {"message",
             "Authentication successful. Client will create fresh connections per request with automatic token refresh."}};
        res.body() = successResponse.dump();
        LOG_INFO("Proxy configured for target: {}://{}:{}", protocol, host,
                 port);
    }
    catch (const json::exception& e)
    {
        res.result(http::status::bad_request);
        json errorResponse = {{"error", "Invalid JSON format"},
                              {"details", e.what()}};
        res.body() = errorResponse.dump();
        LOG_ERROR("JSON parsing error: {}", e.what());
        proxyState.reset();
    }
    catch (const std::exception& e)
    {
        res.result(http::status::internal_server_error);
        json errorResponse = {{"error", "Configuration failed"},
                              {"details", e.what()}};
        res.body() = errorResponse.dump();
        LOG_ERROR("Configuration error: {}", e.what());
        proxyState.reset();
    }

    res.prepare_payload();
    co_return res;
}

// Handler to forward Redfish requests to the configured target server
net::awaitable<Response> handleProxyRequest(Request& req,
                                            const http_function& params)
{
    LOG_INFO("Proxy handler called for: {} {}",
             std::string(req.method_string()), std::string(req.target()));

    Response res{http::status::ok, req.version()};
    res.set(http::field::content_type, "application/json");
    res.keep_alive(req.keep_alive());

    // Check if proxy is configured
    if (!proxyState.configured || !proxyState.client)
    {
        res.result(http::status::service_unavailable);
        json errorResponse = {
            {"error", "Proxy not configured"},
            {"message",
             "Please configure the proxy using POST /redfish/v1/proxy/config"}};
        res.body() = errorResponse.dump();
        res.prepare_payload();
        co_return res;
    }

    try
    {
        // Prepare request to forward - use original request target
        // Keep-alive set to false to avoid connection timeout issues
        RedfishClient::Request clientReq;
        clientReq.withMethod(req.method())
            .withTarget(std::string(req.target()))
            .withBody(req.body())
            .witKeepAlive(false); // Disable keep-alive due to server timeouts

        // Copy relevant headers from original request
        std::map<std::string, std::string> headers;
        for (const auto& field : req)
        {
            std::string name = std::string(field.name_string());
            std::string value = std::string(field.value());

            // Skip host and authorization headers as they will be set by client
            if (name != "host" && name != "Host" && name != "authorization" &&
                name != "Authorization" && name != "X-Auth-Token")
            {
                headers[name] = value;
            }
        }
        clientReq.withHeaders(headers);

        // Execute the forwarded request using stored client
        // Client creates fresh WebClient per request and handles token refresh
        // automatically
        auto [ec, targetRes] = co_await proxyState.client->execute(clientReq);

        if (ec)
        {
            res.result(http::status::bad_gateway);
            json errorResponse = {
                {"error", "Failed to communicate with target server"},
                {"details", ec.message()}};
            res.body() = errorResponse.dump();
            res.prepare_payload();
            LOG_ERROR("Request forwarding failed: {}", ec.message());
            co_return res;
        }

        // Forward the response from target server
        res.result(targetRes.result());
        res.body() = targetRes.body();

        // Copy response headers
        for (const auto& field : targetRes)
        {
            res.set(field.name_string(), field.value());
        }

        res.prepare_payload();
        LOG_DEBUG("Forwarded request to {} - Status: {}",
                  std::string(req.target()),
                  static_cast<int>(targetRes.result()));
    }
    catch (const std::exception& e)
    {
        res.result(http::status::internal_server_error);
        json errorResponse = {{"error", "Internal proxy error"},
                              {"details", e.what()}};
        res.body() = errorResponse.dump();
        res.prepare_payload();
        LOG_ERROR("Exception in proxy handler: {}", e.what());
    }

    co_return res;
}

int main(int argc, const char* argv[])
{
    reactor::getLogger().setLogLevel(reactor::LogLevel::INFO);

    try
    {
        // Parse command line arguments
        auto [portArg] = getArgs(parseCommandline(argc, argv), "--port,-p");

        int port = 8443; // Default port
        if (portArg.has_value())
        {
            try
            {
                port = std::stoi(std::string(portArg.value()));
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Invalid port number: {}", portArg.value());
                return EXIT_FAILURE;
            }
        }

        LOG_INFO("Starting Redfish Proxy Server on port {}", port);
        LOG_INFO(
            "Configuration endpoint: POST https://localhost:{}/redfish/v1/proxy/config",
            port);
        LOG_INFO("Proxy endpoint: ALL https://localhost:{}/redfish/v1/*", port);

        net::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_server);

        // Load server certificate and key (you may need to adjust paths)
        // For testing, you might need to generate self-signed certificates
        try
        {
            ctx.use_certificate_chain_file("/etc/ssl/certs/https/server.pem");
            ctx.use_private_key_file("/etc/ssl/certs/https/server.pem",
                                     ssl::context::pem);
        }
        catch (const std::exception& e)
        {
            LOG_WARNING("Failed to load SSL certificates: {}", e.what());
            LOG_WARNING("Attempting to continue without SSL certificates");
        }

        // Create HTTP router
        HttpRouter router;
        router.setIoContext(std::ref(ioc));

        // Register configuration endpoint (specific route)
        router.add_post_handler("/redfish/v1/proxy/config",
                                handleConfigEndpoint);

        // Set fallback handler to catch all unmatched requests
        // This will forward any Redfish request that doesn't match the config
        // endpoint
        router.set_fallback_handler(handleProxyRequest);

        // Create acceptor and server
        TcpStreamType acceptor(ioc.get_executor(), port, ctx);
        HttpServer server(ioc, acceptor, router);

        LOG_INFO("Redfish Proxy Server started successfully");

        // Run the IO context
        ioc.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Fatal error: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
