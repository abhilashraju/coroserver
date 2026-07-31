#include "command_line_parser.hpp"
#include "graphql_redfish_executor.hpp"
#include "graphql_redfish_provider.hpp"
#include "graphql_redfish_schema.hpp"
#include "http_server.hpp"
#include "logger.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>

using namespace NSNAME;

int main(int argc, const char* argv[])
{
    try
    {
        auto [cert, port, host, targetPort, user, password] = getArgs(
            parseCommandline(argc, argv), "--cert,-c", "--port,-p",
            "--host,-h", "--target-port,-t", "--user,-u", "--password,-w");

        boost::asio::io_context ioContext;

        RedfishProviderConfig providerConfig;
        providerConfig.host = host ? std::string(*host) : "localhost";
        providerConfig.port = targetPort ? std::string(*targetPort) : "443";
        providerConfig.username = user ? std::string(*user) : "root";
        providerConfig.password = password ? std::string(*password) : "0penBmc";

        auto provider =
            std::make_shared<HttpRedfishProvider>(ioContext, providerConfig);
        auto executor = std::make_shared<RedfishGraphQLExecutor>(
            buildRedfishTypedSchema(), provider);

        boost::asio::ssl::context sslContext(
            boost::asio::ssl::context::sslv23);
        sslContext.set_options(boost::asio::ssl::context::default_workarounds |
                               boost::asio::ssl::context::no_sslv2 |
                               boost::asio::ssl::context::single_dh_use);

        std::string certDir = cert ? std::string(*cert) : ".";
        sslContext.use_certificate_chain_file(certDir + "/server-cert.pem");
        sslContext.use_private_key_file(certDir + "/server-key.pem",
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
                      {"managers", "List Redfish managers"}}}};
                return make_success_response(schemaDoc, http::status::ok,
                                             req.version());
            });

        int serverPort = port ? std::stoi(std::string(*port)) : 8444;
        TcpStreamType acceptor(ioContext.get_executor(), serverPort, sslContext);
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
