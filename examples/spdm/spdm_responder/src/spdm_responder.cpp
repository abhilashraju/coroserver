#include "spdm_responder.hpp"

#include "command_line_parser.hpp"
#include "responder_object.hpp"
#include "spdm_io_redirect.hpp"

#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/bus.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

using namespace reactor;

// Utility function to parse and validate port number
std::optional<int> parsePort(const std::string& portStr)
{
    try
    {
        int port = std::stoi(portStr);
        if (port < 1 || port > 65535)
        {
            LOG_ERROR("Port number must be between 1 and 65535");
            return std::nullopt;
        }
        return port;
    }
    catch (const std::invalid_argument& e)
    {
        LOG_ERROR("Invalid port number format: {}", e.what());
        return std::nullopt;
    }
    catch (const std::out_of_range& e)
    {
        LOG_ERROR("Port number out of range: {}", e.what());
        return std::nullopt;
    }
}

int main(int argc, const char* argv[])
{
    auto [port, deviceId] = reactor::getArgs(
        reactor::parseCommandline(argc, argv), "--port,-p", "--device-id,-d");
    reactor::getLogger().setLogLevel(reactor::LogLevel::DEBUG);
    boost::asio::io_context io_context;
    auto conn = std::make_shared<sdbusplus::asio::connection>(io_context);

    // Enable SPDM library output redirection to logger
    spdm_io_redirect::enableSpdmLogging(io_context);
    if (!port.has_value())
    {
        LOG_ERROR("Port number cannot be empty");
        return 1;
    }

    // Use default device ID if not provided
    std::string device =
        deviceId.has_value() ? std::string(deviceId.value()) : "0";
    reactor::getWorkerPool(2);
    // Create D-Bus object for attestation status
    auto dbusObject = std::make_shared<SpdmResponderObject>(conn, device);

    auto portNumber = parsePort(port.value().data());
    if (!portNumber.has_value())
    {
        return 1;
    }

    SpdmTcpServer server(io_context, portNumber.value());
    conn->request_name("xyz.openbmc_project.spdm.responder");
    SpdmResponder responder(server, dbusObject);

    responder.run();
    io_context.run();
    return 0;
}
