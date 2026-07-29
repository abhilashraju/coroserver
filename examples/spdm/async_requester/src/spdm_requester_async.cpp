/**
 * spdm_requester_async.cpp — Async SPDM requester service entry point.
 *
 * Mirrors spdm_requester.cpp in terms of functionality:
 *  - LLDP neighbour discovery (signal watcher + initial property query)
 *  - AsyncComponentIntegrity D-Bus objects per discovered device
 *  - Retry on connection failure
 *
 * All SPDM protocol work is driven via AsyncSpdmRequester (co_await-based).
 * The io_context is never blocked — no worker pool is needed.
 *
 * Usage: spdm_requester_async -p <port> [-i <interface>]
 */

#include "async_component_integrity.hpp"
#include "command_line_parser.hpp"
#include "dbusproperty_watcher.hpp"
#include "lldp_neighbour_handlers.hpp"
#include "spdm_io_redirect.hpp"

#include <boost/asio.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus/match.hpp>

#include <format>
#include <map>
#include <string>

using namespace reactor;
namespace net = boost::asio;

// Define static member
std::map<std::string, std::shared_ptr<spdm_async::AsyncComponentIntegrity>>
    spdm_async::AsyncComponentIntegrity::spdmDevices;

auto updateNeighbourDetails(net::io_context& io_context,
                            std::shared_ptr<sdbusplus::asio::connection> conn,
                            int iport)
{
    return [&io_context, conn,
            iport](const std::string& address,
                   const std::string& name) -> net::awaitable<void> {
        LOG_INFO("Neighbour LLDP Address: {} Name: {}", address, name);
        spdm_async::AsyncComponentIntegrity::DeviceInfo deviceInfo{
            spdm_async::AsyncComponentIntegrity::TcpDeviceInfo{address, iport}};
        spdm_async::AsyncComponentIntegrity::addComponentIntegrity(
            io_context, conn, std::move(deviceInfo));
        co_return;
    };
}

int main(int argc, const char* argv[])
{
    try
    {
        auto [port, iface] = getArgs(parseCommandline(argc, argv), "--port,-p",
                                     "--interface,-i");
        int iport = 2448;
        if (port)
        {
            iport = std::atoi(port.value().data());
        }
        std::string ifaceName = "eth0";
        if (iface)
        {
            ifaceName = std::string(iface.value());
        }

        reactor::getLogger().setLogLevel(reactor::LogLevel::DEBUG);
        net::io_context io;
        auto conn = std::make_shared<sdbusplus::asio::connection>(io);

        spdm_io_redirect::enableSpdmLogging(io);

        // Connect to the default local responder (mirrors sync requester)
        spdm_async::AsyncComponentIntegrity::DeviceInfo deviceInfo{
            spdm_async::AsyncComponentIntegrity::TcpDeviceInfo{"127.0.0.1",
                                                               iport}};
        spdm_async::AsyncComponentIntegrity::addComponentIntegrity(
            io, conn, std::move(deviceInfo));

        // Watch for new LLDP neighbours via InterfacesAdded signal
        DbusSignalWatcher<sdbusplus::message_t>::watch(
            io, conn,
            makeNeighbourDiscoveryHandler(
                updateNeighbourDetails(io, conn, iport)),
            sdbusplus::bus::match::rules::interfacesAddedAtPath(
                std::format(LLDP_REC_PATH, ifaceName)));

        // Query existing LLDP neighbours at startup
        net::co_spawn(
            io,
            makeNeighbourUpdateHandler(
                conn, ifaceName, updateNeighbourDetails(io, conn, iport)),
            net::detached);

        conn->request_name("xyz.openbmc_project.spdm.async_requester");
        io.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception in main: {}", e.what());
        return 1;
    }

    return 0;
}
