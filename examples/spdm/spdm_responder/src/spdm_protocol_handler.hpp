#pragma once
#include "logger.hpp"
#include "responder_init.hpp"
#include "responder_object.hpp"
#include "spdmglobal.hpp"

#include <cstdint>
extern "C"
{
#include <library/spdm_responder_lib.h>
}

/**
 * @brief Handles SPDM protocol logic and message dispatching
 *
 * This class is responsible for SPDM protocol operations including
 * message dispatching, retry logic, and protocol state management.
 */
class SpdmProtocolHandler
{
  public:
    SpdmProtocolHandler() : dispatchRetryCount_(0) {}

    bool initializeResponder(void* spdmContext)
    {
        if (!spdmResponderInit(spdmContext))
        {
            LOG_ERROR(
                "Failed to initialize SPDM responder: certificate chain loading failed");
            return false;
        }
        return true;
    }

    bool dispatchMessage(void* spdmContext)
    {
        libspdm_return_t status =
            libspdm_responder_dispatch_message(spdmContext);

        if (status == LIBSPDM_STATUS_SUCCESS)
        {
            dispatchRetryCount_ = 0; // Reset retry count on success
            return true;
        }

        return false;
    }

    libspdm_return_t getLastDispatchStatus(void* spdmContext)
    {
        return libspdm_responder_dispatch_message(spdmContext);
    }

    bool checkRetryLimit(const char* errorMsg)
    {
        dispatchRetryCount_++;
        if (dispatchRetryCount_ >= maxDispatchRetries_)
        {
            LOG_ERROR("%s - terminating session", errorMsg);
            return false;
        }
        return true;
    }

    void resetRetryCount()
    {
        dispatchRetryCount_ = 0;
    }

    static void logSpdmMessageType(const void* message, size_t message_size)
    {
        if (message_size < sizeof(spdm_message_header_t))
        {
            return;
        }

        const auto* header = static_cast<const spdm_message_header_t*>(message);
        uint8_t request_code = header->request_response_code;

        switch (request_code)
        {
            case SPDM_GET_DIGESTS:
                LOG_INFO(
                    "SPDM Responder: Received GET_DIGESTS request (0x{:02X})",
                    request_code);
                break;
            case SPDM_GET_CERTIFICATE:
                LOG_INFO(
                    "SPDM Responder: Received GET_CERTIFICATE request (0x{:02X})",
                    request_code);
                break;
            case SPDM_GET_MEASUREMENTS:
                LOG_INFO(
                    "SPDM Responder: Received GET_MEASUREMENTS request (0x{:02X})",
                    request_code);
                break;
            case SPDM_CHALLENGE:
                LOG_INFO(
                    "SPDM Responder: Received CHALLENGE request (0x{:02X})",
                    request_code);
                break;
            case SPDM_GET_VERSION:
                LOG_INFO(
                    "SPDM Responder: Received GET_VERSION request (0x{:02X})",
                    request_code);
                break;
            case SPDM_GET_CAPABILITIES:
                LOG_INFO(
                    "SPDM Responder: Received GET_CAPABILITIES request (0x{:02X})",
                    request_code);
                break;
            case SPDM_NEGOTIATE_ALGORITHMS:
                LOG_INFO(
                    "SPDM Responder: Received NEGOTIATE_ALGORITHMS request (0x{:02X})",
                    request_code);
                break;
            default:
                LOG_DEBUG("SPDM Responder: Received request code 0x{:02X}",
                          request_code);
                break;
        }
    }

  private:
    size_t dispatchRetryCount_;
    static constexpr size_t maxDispatchRetries_ = 100;
};
