#include "command_line_parser.hpp"
#include "graphql_dbus_bridge.hpp"
#include "logger.hpp"

#include <boost/asio.hpp>
#include <sdbusplus/asio/connection.hpp>

int main(int argc, const char* argv[])
{
    reactor::getLogger().setLogLevel(reactor::LogLevel::DEBUG);
    try
    {
        auto [configPath] =
            getArgs(parseCommandline(argc, argv), "--config,-c");

        if (!configPath.has_value())
        {
            std::cerr <<
                "Usage: graphql_dbus_client -c <config.json>\n"
                "\n"
                "  config.json keys:\n"
                "    host, port, subscriptions[]\n"
                "    Each subscription: name, dbus_path, dbus_interface,\n"
                "                       query, interval_seconds, field_map\n"
                "\n"
                "  See config/satellite_queries.json for a full example.\n";
            return EXIT_FAILURE;
        }

        boost::asio::io_context ioc;

        // Request the well-known DBus service name under which all Satellite
        // objects will be published.
        auto conn = std::make_shared<sdbusplus::asio::connection>(ioc);
        conn->request_name("xyz.openbmc_project.Satellite.GraphqlClient");

        auto maybeBridge =
            GraphqlDbusBridge::create(ioc, conn, std::string(*configPath));
        if (!maybeBridge)
        {
            LOG_ERROR("Bridge init failed: {}", maybeBridge.error());
            return EXIT_FAILURE;
        }
        (*maybeBridge)->start();

        LOG_INFO("graphql_dbus_client running — objects under "
                 "/xyz/openbmc_project/Satellite");
        ioc.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Fatal: {}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
