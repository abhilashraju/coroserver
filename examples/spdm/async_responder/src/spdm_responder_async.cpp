/**
 * spdm_responder_async.cpp — Async SPDM responder service entry point.
 *
 * Mirrors spdm_responder.cpp exactly in terms of functionality:
 *  - D-Bus SpdmResponderObject creation
 *  - Certificate loading and trust-store setup
 *  - Custom message handlers (PUSH_CERTIFICATE, PULL_CERTIFICATE,
 *    SET_PROVISIONED)
 *
 * All SPDM protocol dispatch is driven by AsyncSpdmTcpServer /
 * AsyncSpdmResponder via co_await — the io_context is never blocked.
 * No worker pool is needed.
 *
 * Usage: spdm_responder_async -p <port> [-d <device-id>]
 */

#include "async_spdm_tcp_server.hpp"
#include "cert_exchange_handler.hpp"
#include "command_line_parser.hpp"
#include "custom_message_handlers.hpp"
#include "responder_object.hpp"
#include "spdm_io_redirect.hpp"

#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/bus.hpp>

#include <fstream>
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

    spdm_io_redirect::enableSpdmLogging(io_context);

    auto dbusObject = std::make_shared<SpdmResponderObject>(conn, device);

    CertificateExchangeHandler certExchangeHandler(
        std::make_shared<FileCertificateTrustStore>("/etc/ssl/certs/authority"),
        "responder");

    // Mirrors SpdmResponder::loadResponderCertificate
    {
        std::string certPath = "/etc/ssl/certs/self_ca.pem";
        std::ifstream certFile(certPath);
        if (certFile.good())
        {
            certExchangeHandler.setResponderCertificatePath(
                certPath, CertificateFormat::PEM);
            LOG_INFO("Set responder certificate path: {}", certPath);
        }
        else
        {
            LOG_ERROR("Responder certificate not found: {}", certPath);
        }
    }

    std::map<uint8_t, CustomMessageHandler> handlers;
    handlers[SPDM_PUSH_CERTIFICATE] =
        CustomMessageHandlerFactory::createPushCertificateHandler(
            certExchangeHandler);
    handlers[SPDM_PULL_CERTIFICATE] =
        CustomMessageHandlerFactory::createPullCertificateHandler(
            certExchangeHandler);
    handlers[SPDM_SET_PROVISIONED] =
        CustomMessageHandlerFactory::createSetProvisionedHandler(
            certExchangeHandler, dbusObject);

    AsyncSpdmTcpServer server(io_context,
                              static_cast<uint16_t>(portNumber.value()),
                              std::move(handlers));
    server.accept();

    LOG_INFO("Async SPDM responder listening on port {}", portNumber.value());

    conn->request_name("xyz.openbmc_project.spdm.async_responder");
    io_context.run();
    return 0;
}
