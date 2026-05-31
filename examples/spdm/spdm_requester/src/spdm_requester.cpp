#include "spdm_requester.hpp"

#include "command_line_parser.hpp"
#include "component_integrity.hpp"
#include "dbusproperty_watcher.hpp"
#include "lldp_neighbour_handlers.hpp"
#include "spdm_io_redirect.hpp"
#include "worker.hpp"

#include <format>
#include <map>
#include <vector>

using namespace reactor;
using ResultType = std::pair<bool, GetMeasurementsReturnType>;

// Define static member
std::map<std::string, std::shared_ptr<spdm::ComponentIntegrity>>
    spdm::ComponentIntegrity::spdmDevices;
auto updateNeighbourDetails(net::io_context& io_context,
                            std::shared_ptr<sdbusplus::asio::connection> conn,
                            int iport)
{
    return [&io_context, conn,
            iport](const std::string& address,
                   const std::string& name) -> net::awaitable<void> {
        LOG_INFO("Neighbour LLDP Address : {} Name : {} ", address, name);
        spdm::ComponentIntegrity::DeviceInfo deviceInfo{
            spdm::ComponentIntegrity::TcpDeviceInfo{address, iport}};
        spdm::ComponentIntegrity::addComponentIntegrity(io_context, conn,
                                                        std::move(deviceInfo));
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
        boost::asio::io_context io;
        auto conn = std::make_shared<sdbusplus::asio::connection>(io);

        // Enable SPDM library output redirection to logger
        spdm_io_redirect::enableSpdmLogging(io);

        // Create a simple requester for testing
        spdm::ComponentIntegrity::DeviceInfo deviceInfo{
            spdm::ComponentIntegrity::TcpDeviceInfo{"127.0.0.1", iport}};

        spdm::ComponentIntegrity::addComponentIntegrity(io, conn,
                                                        std::move(deviceInfo));

        DbusSignalWatcher<sdbusplus::message_t>::watch(
            io, conn,
            makeNeighbourDiscoveryHandler(
                updateNeighbourDetails(io, conn, iport)),
            sdbusplus::bus::match::rules::interfacesAddedAtPath(
                std::format(LLDP_REC_PATH, ifaceName)));
        net::co_spawn(
            io,
            makeNeighbourUpdateHandler(conn, ifaceName,
                                       updateNeighbourDetails(io, conn, iport)),
            net::detached);

        conn->request_name("xyz.openbmc_project.spdm.requester");
        io.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception in main: {}", e.what());
        return 1;
    }

    return 0;
}
