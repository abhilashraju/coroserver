
#include "command_line_parser.hpp"
#include "http_server.hpp"
#include "logger.hpp"
#include "pam_functions.hpp"
#include "sdbus_calls.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
int main(int argc, const char* argv[])
{
    try
    {
        auto [cert] = getArgs(parseCommandline(argc, argv), "--cert,-c");

        setenv("DBUS_SYSTEM_BUS_ADDRESS",
               "unix:path=/var/run/dbus/system_bus_socket", 1);
        boost::asio::io_context io_context;

        auto conn = std::make_shared<sdbusplus::asio::connection>(io_context);
        constexpr std::string_view busName = "xyz.openbmc_project.usermanager";
        constexpr std::string_view objPath = "/xyz/openbmc_project/user";
        constexpr std::string_view interface =
            "xyz.openbmc_project.usermanager.User";
        conn->request_name(busName.data());
        auto dbusServer = sdbusplus::asio::object_server(conn);
        std::shared_ptr<sdbusplus::asio::dbus_interface> iface =
            dbusServer.add_interface(objPath.data(), interface.data());
        // test generic properties

        iface->register_property_rw<bool>(
            "mfaenbled", sdbusplus::vtable::property_::emits_change,
            [](const auto& newPropertyValue, auto& prop) {
                prop = newPropertyValue;
                return true;
            },
            [](const auto& prop) { return prop; });

        iface->register_method("createSecretKey",
                               [](const std::string& userName) {
                                   return createSecretKey(userName);
                               });

        iface->initialize();

        boost::asio::ssl::context ssl_context(
            boost::asio::ssl::context::sslv23);

        // Load server certificate and private key
        ssl_context.set_options(boost::asio::ssl::context::default_workarounds |
                                boost::asio::ssl::context::no_sslv2 |
                                boost::asio::ssl::context::single_dh_use);
        std::cerr << "Cert Loading: \n";
        std::string certDir = "/etc/ssl/private";
        if (cert)
        {
            certDir = std::string(*cert);
        }
        ssl_context.use_certificate_chain_file(certDir + "/server-cert.pem");
        ssl_context.use_private_key_file(certDir + "/server-key.pem",
                                         boost::asio::ssl::context::pem);
        std::cerr << "Cert Loaded: \n";
        std::string socket_path = "/tmp/http_server.sock";
        std::remove(socket_path.c_str());
        HttpRouter router;
        router.setIoContext(io_context);
        // TcpStreamType acceptor(io_context, 8080, ssl_context);
        UnixStreamType unixAcceptor(io_context.get_executor(), socket_path,
                                    ssl_context);
        HttpServer server(io_context, unixAcceptor, router);
        router.add_get_handler(
            "/mfaenabled",
            [&](auto& req, auto& params) -> net::awaitable<Response> {
                auto [ec, enabled] = co_await getProperty<bool>(
                    *conn, busName.data(), objPath.data(), interface.data(),
                    "mfaenbled");
                if (ec)
                {
                    LOG_ERROR("Error getting property: {}", ec.message());
                    co_return make_internal_server_error(
                        "Internal Server Error", req.version());
                }
                nlohmann::json jsonResponse;
                jsonResponse["mfaenbled"] = enabled;
                co_return make_success_response(jsonResponse, http::status::ok,
                                                req.version());
            });
        using variant = std::variant<bool, std::string>;
        router.add_get_handler(
            "/allmfaproperties",
            [&](auto& req, auto& params) -> net::awaitable<Response> {
                auto [ec, props] = co_await getAllProperties<variant>(
                    *conn, busName.data(), objPath.data(), interface.data());
                if (ec)
                {
                    LOG_ERROR("Error getting all properties: {}", ec.message());
                    co_return make_internal_server_error(
                        "Internal Server Error", req.version());
                }
                nlohmann::json jsonResponse;
                for (auto& prop : props)
                {
                    std::visit(
                        [&](auto val) { jsonResponse[prop.first] = val; },
                        prop.second);
                }

                co_return make_success_response(jsonResponse, http::status::ok,
                                                req.version());
            });
        router.add_post_handler(
            "/mfaenabled",
            [&](auto& req, auto& params) -> net::awaitable<Response> {
                net::steady_timer timer(io_context);

                auto [ec] = co_await setProperty(
                    *conn, busName.data(), objPath.data(), interface.data(),
                    "mfaenbled", params["mfaenbled"] == "true");
                if (ec)
                {
                    LOG_ERROR("Error setting property: {}", ec.message());
                    co_return make_internal_server_error(
                        "Internal Server Error", req.version());
                }
                nlohmann::json jsonResponse;
                jsonResponse["satatus"] = "sucess";
                co_return make_success_response(jsonResponse, http::status::ok,
                                                req.version());
            });
        router.add_post_handler(
            "/bodytest",
            [&](auto& req, auto& params) -> net::awaitable<Response> {
                net::steady_timer timer(io_context);

                auto body = req.body();
                nlohmann::json data;
                data["name"] = "test";
                data["age"] = 32;
                co_return make_success_response(data, http::status::ok,
                                                req.version());
            });

        router.add_post_handler(
            "/createSecretKey",
            [](Request& req,
               const http_function& params) -> net::awaitable<Response> {
                auto userName = params["userName"];
                if (userName.empty())
                {
                    co_return make_bad_request_error("userName is required",
                                                     req.version());
                }
                auto secretKey = createSecretKey(userName);
                co_return make_success_response(secretKey, http::status::ok,
                                                req.version());
            });
        router.add_get_handler(
            "/getSubTree",
            [&](Request& req,
                const http_function& params) -> net::awaitable<Response> {
                using SubTreeType = std::vector<std::pair<
                    std::string, std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>>;
                nlohmann::json data;
                try
                {
                    data = nlohmann::json::parse(req.body(), nullptr, false);
                }
                catch (const nlohmann::json::parse_error& e)
                {
                    co_return make_bad_request_error("Invalid JSON",
                                                     req.version());
                }

                if (data.is_discarded())
                {
                    co_return make_bad_request_error("Invalid JSON",
                                                     req.version());
                }
                auto [ec, subtree] = co_await getSubTree<SubTreeType>(
                    *conn, data["path"], data["depth"], data["interfaces"]);
                if (ec)
                {
                    LOG_ERROR("Error getting subtree: {}", ec.message());
                    co_return make_internal_server_error(
                        "Internal Server Error", req.version());
                }
                nlohmann::json jsonResponse;
                for (auto& item : subtree)
                {
                    jsonResponse[item.first] = item.second;
                }
                co_return make_success_response(jsonResponse, http::status::ok,
                                                req.version());
            });

        io_context.run();
    }
    catch (std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return 1;
    }

    return 0;
}
