/**
 * async_main.cpp — Async SPDM responder entry point.
 *
 * Demonstrates fully asynchronous SPDM responder operation:
 *  - Multiple client connections served concurrently on a single io_context
 *    thread.
 *  - No blocking I/O anywhere; all socket reads/writes via async_read_some /
 *    async_write.
 *  - Timeouts handled by TimedStreamer; no SO_RCVTIMEO/SO_SNDTIMEO.
 *
 * Usage: async_spdm_responder --port 2323 [--device-id 0]
 */

#include "async_spdm_tcp_server.hpp"
#include "command_line_parser.hpp"
#include "responder_object.hpp"
#include "spdm_io_redirect.hpp"

#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/bus.hpp>

#include <optional>
#include <string>

using namespace reactor;

static std::optional<int> parsePort(const std::string& portStr)
{
    try
    {
        int port = std::stoi(portStr);
        if (port < 1 || port > 65535)
        {
            LOG_ERROR("Port must be between 1 and 65535");
            return std::nullopt;
        }
        return port;
    }
    catch (...)
    {
        LOG_ERROR("Invalid port: {}", portStr);
        return std::nullopt;
    }
}

int main(int argc, const char* argv[])
{
    auto [port, deviceId] = reactor::getArgs(
        reactor::parseCommandline(argc, argv), "--port,-p", "--device-id,-d");
    reactor::getLogger().setLogLevel(reactor::LogLevel::DEBUG);

    if (!port.has_value())
    {
        LOG_ERROR("--port is required");
        return 1;
    }

    auto portNumber = parsePort(port.value().data());
    if (!portNumber.has_value())
    {
        return 1;
    }

    std::string device =
        deviceId.has_value() ? std::string(deviceId.value()) : "0";

    boost::asio::io_context io_context;
    auto conn = std::make_shared<sdbusplus::asio::connection>(io_context);

    // Redirect libspdm debug output to logger
    spdm_io_redirect::enableSpdmLogging(io_context);

    auto dbusObject = std::make_shared<SpdmResponderObject>(conn, device);

    AsyncSpdmTcpServer server(io_context,
                              static_cast<uint16_t>(portNumber.value()));
    server.accept();

    LOG_INFO("Async SPDM responder listening on port {}", portNumber.value());

    conn->request_name("xyz.openbmc_project.spdm.async_responder");
    io_context.run();
    return 0;
}
