#pragma once

#include "cert_exchange_handler.hpp"
#include "spdm_custom_messages.hpp"

#include <logger.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

// Forward declaration
class SpdmResponderObject;

// Custom message handler function type
// Takes: request data, returns: response data
using CustomMessageHandler =
    std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>;

// Custom message handler context - holds all dependencies needed by handlers

// Factory class for creating custom message handlers
class CustomMessageHandlerFactory
{
  public:
    // Create handler for PUSH_CERTIFICATE
    static CustomMessageHandler createPushCertificateHandler(
        CertificateExchangeHandler& certExchangeHandler)
    {
        return [&certExchangeHandler](const std::vector<uint8_t>& request) {
            LOG_INFO("Handling PUSH_CERTIFICATE request");
            return certExchangeHandler.handlePushCertificate(request);
        };
    }

    // Create handler for PULL_CERTIFICATE
    static CustomMessageHandler createPullCertificateHandler(
        CertificateExchangeHandler& certExchangeHandler)
    {
        return [&certExchangeHandler](const std::vector<uint8_t>& request) {
            LOG_INFO("Handling PULL_CERTIFICATE request");
            return certExchangeHandler.handlePullCertificate(request);
        };
    }

    // Create handler for SET_PROVISIONED
    static CustomMessageHandler createSetProvisionedHandler(
        CertificateExchangeHandler& certExchangeHandler,
        std::shared_ptr<SpdmResponderObject> responderObject)
    {
        return [&certExchangeHandler,
                responderObject](const std::vector<uint8_t>& request) {
            LOG_INFO("Handling SET_PROVISIONED request");

            return certExchangeHandler.handleSetProvisioned(request,
                                                            responderObject);
        };
    }
};
