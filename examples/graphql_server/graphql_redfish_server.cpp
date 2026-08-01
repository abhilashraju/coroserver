#include "command_line_parser.hpp"
#include "graphql_redfish_executor.hpp"
#include "graphql_redfish_provider.hpp"
#include "graphql_redfish_schema.hpp"
#include "http_server.hpp"
#include "logger.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>

using namespace NSNAME;

int main(int argc, const char* argv[])
{
    try
    {
        auto [cert, port, host, targetPort, user, password, clientCert,
              clientKey] =
            getArgs(parseCommandline(argc, argv), "--cert,-c", "--port,-p",
                    "--host,-h", "--target-port,-t", "--user,-u",
                    "--password,-w", "--client-cert,-C", "--client-key,-K");

        // Require at least one authentication method.
        bool hasBasicAuth = user.has_value() && password.has_value();
        bool hasMtls = clientCert.has_value() && clientKey.has_value();
        if (!hasBasicAuth && !hasMtls)
        {
            LOG_ERROR(
                "Authentication required: provide either "
                "--user/-u and --password/-w (basic auth) "
                "or --client-cert/-C and --client-key/-K (mTLS)");
            return 1;
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
        auto executor = std::make_shared<RedfishGraphQLExecutor>(
            buildRedfishTypedSchema(), provider);

        boost::asio::ssl::context sslContext(boost::asio::ssl::context::sslv23);
        sslContext.set_options(boost::asio::ssl::context::default_workarounds |
                               boost::asio::ssl::context::no_sslv2 |
                               boost::asio::ssl::context::single_dh_use);

        std::string certFile = cert ? std::string(*cert) + "/server-cert.pem"
                                    : "/etc/ssl/certs/https/server_cert.pem";
        std::string keyFile = cert ? std::string(*cert) + "/server-key.pem"
                                   : "/etc/ssl/private/server_pkey.pem";
        sslContext.use_certificate_chain_file(certFile);
        sslContext.use_private_key_file(keyFile,
                                        boost::asio::ssl::context::pem);

        HttpRouter router;
        router.setIoContext(ioContext);

        router.add_post_handler(
            "/graphql",
            [executor](Request& req, const http_function& params)
                -> net::awaitable<Response> {
                nlohmann::json requestBody;

                try
                {
                    requestBody =
                        nlohmann::json::parse(req.body(), nullptr, false);
                }
                catch (const nlohmann::json::parse_error&)
                {
                    co_return make_bad_request_error(
                        "Invalid JSON in request body", req.version());
                }

                if (requestBody.is_discarded() ||
                    !requestBody.contains("query"))
                {
                    co_return make_bad_request_error(
                        "Missing 'query' field in request", req.version());
                }

                std::string query = requestBody["query"].get<std::string>();
                nlohmann::json variables = requestBody.contains("variables")
                                               ? requestBody["variables"]
                                               : nlohmann::json::object();

                nlohmann::json response =
                    co_await executor->execute(query, variables);
                co_return make_success_response(response, http::status::ok,
                                                req.version());
            });

        router.add_get_handler(
            "/health",
            [](Request& req, const http_function& params) -> Response {
                nlohmann::json response = {
                    {"status", "healthy"},
                    {"service", "Redfish GraphQL Server"}};
                return make_success_response(response, http::status::ok,
                                             req.version());
            });

        router.add_get_handler(
            "/schema",
            [](Request& req, const http_function& params) -> Response {
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
                       "Stream live updates for a Chassis (arg: id)"}}}};
                return make_success_response(schemaDoc, http::status::ok,
                                             req.version());
            });

        // SSE subscription endpoint
        // GET /graphql/subscribe?query=subscription{systemStatus(id:"1"){...}}
        // Optional: &interval=5  (poll interval in seconds, default 5)
        router.add_sse_handler(
            "/graphql/subscribe",
            [executor](Request& req, const http_function& params,
                       SseWriter writer) -> net::awaitable<void> {
                // parse_function already split and URL-decoded the query string
                std::string query = params["query"];
                std::string intervalStr = params["interval"];

                if (query.empty())
                {
                    nlohmann::json err = {{"errors",
                                           {{{"message",
                                              "Missing 'query' query-string "
                                              "parameter"}}}}};
                    co_await writer.write(err.dump());
                    co_return;
                }

                int intervalSecs =
                    intervalStr.empty() ? 5 : std::stoi(intervalStr);
                auto interval = std::chrono::seconds(intervalSecs);

                co_await executor->executeSubscription(
                    query, nlohmann::json::object(), interval,
                    [&writer](nlohmann::json event) -> net::awaitable<bool> {
                        // Serialize the event and push it to the SSE stream.
                        // writer.write returns false when the client has gone.
                        bool ok = co_await writer.write(event.dump());
                        co_return ok;
                    });
            });

        int serverPort = port ? std::stoi(std::string(*port)) : 8444;
        TcpStreamType acceptor(ioContext.get_executor(), serverPort,
                               sslContext);
        HttpServer server(ioContext, acceptor, router);

        LOG_INFO("Redfish GraphQL Server started on port {}", serverPort);
        LOG_INFO("Querying Redfish target {}:{}", providerConfig.host,
                 providerConfig.port);

        ioContext.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return 1;
    }

    return 0;
}
