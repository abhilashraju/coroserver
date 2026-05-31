#pragma once

#include "spdm_measurement_utils.hpp"
#include "spdm_tcp_server.hpp"
#include "worker.hpp"

#include <boost/asio.hpp>

#include <fstream>
#include <functional>
#include <memory>

struct SpdmResponder
{
  public:
    SpdmResponder(SpdmTcpServer& server,
                  std::shared_ptr<SpdmResponderObject> dbusObj = nullptr,
                  const std::string& trustStorePath = "/etc/ssl/certs") :
        server(server), dbusObject(dbusObj),
        certExchangeHandler(
            std::make_shared<FileCertificateTrustStore>(trustStorePath),
            "responder")

    {
        loadResponderCertificate();
        handlers[SPDM_PUSH_CERTIFICATE] =
            CustomMessageHandlerFactory::createPushCertificateHandler(
                certExchangeHandler);
        handlers[SPDM_PULL_CERTIFICATE] =
            CustomMessageHandlerFactory::createPullCertificateHandler(
                certExchangeHandler);
        handlers[SPDM_SET_PROVISIONED] =
            CustomMessageHandlerFactory::createSetProvisionedHandler(
                certExchangeHandler, dbusObject);
        server.setSessionHandler(
            std::bind_front(&SpdmResponder::handleSession, this));
    }
    ~SpdmResponder() {}

    void run()
    {
        server.accept();
    }
    static boost::asio::awaitable<void> handleSession(
        SpdmResponder* self, std::shared_ptr<SpdmSession> session)
    {
        session->setCustomMessageHandlers(self->handlers);

        auto spmdmtask = [session]() -> void { session->run(); };

        auto [ec] = co_await reactor::asyncCall(session->ioContext,
                                                std::move(spmdmtask));
        if (ec)
        {
            LOG_ERROR("Error running SPDM session: {}", ec.message());
        }
        co_return;
    }

    void loadResponderCertificate()
    {
        // Set certificate path for lazy loading instead of loading into memory
        // This is a placeholder - in production, load from secure storage
        std::string certPath = "/etc/ssl/certs/self_ca.pem";

        try
        {
            // Verify file exists before setting path
            std::ifstream certFile(certPath);
            if (certFile.good())
            {
                certExchangeHandler.setResponderCertificatePath(
                    certPath, CertificateFormat::PEM);
                LOG_INFO("Set responder certificate path for lazy loading: {}",
                         certPath);
            }
            else
            {
                LOG_ERROR("Responder certificate file not found: {}", certPath);
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to set responder certificate path: {}", e.what());
        }
    }

    SpdmTcpServer& server;
    std::shared_ptr<SpdmResponderObject> dbusObject;
    CertificateExchangeHandler certExchangeHandler;
    std::map<uint8_t, CustomMessageHandler> handlers;
};
