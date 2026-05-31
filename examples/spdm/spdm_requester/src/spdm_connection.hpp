#pragma once
#include "logger.hpp"
#include "requester_init.hpp"
#include "spdm_custom_messages.hpp"
#include "spdm_io_redirect.hpp"
#include "spdm_tcp_client.hpp"
#include "spdmglobal.hpp"

extern "C"
{
#include <internal/libspdm_common_lib.h>
}

#include <boost/asio/io_context.hpp>

#include <memory>
#include <string>

// Global timeout for device send/receive operations (10 seconds in
// microseconds)
constexpr uint64_t DEVICE_IO_TIMEOUT = 10000000;

// Handles SPDM connection management and basic communication
class SpdmConnectionManager :
    public SpdmConnectionTemplate<SpdmConnectionManager>
{
  public:
    SpdmConnectionManager(boost::asio::io_context& io) : client(io)
    {
        libspdm_data_parameter_t parameter = {};
        parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
        uint8_t ct_exponent = 0;
        uint32_t cap_flags = 0;
        libspdm_set_data(spdmContext, LIBSPDM_DATA_CAPABILITY_CT_EXPONENT,
                         &parameter, &ct_exponent, sizeof(ct_exponent));
        libspdm_set_data(spdmContext, LIBSPDM_DATA_CAPABILITY_FLAGS, &parameter,
                         &cap_flags, sizeof(cap_flags));
    }

    ~SpdmConnectionManager() {}

    bool connect(const std::string& host, uint16_t port)
    {
        return !client.connect(host, port);
    }

    bool sayhello()
    {
        return true;
    }

    bool init_connection()
    {
        // Initialize connection (GET_VERSION, GET_CAPABILITIES,
        // NEGOTIATE_ALGORITHMS)
        return spdmClientInit(spdmContext);
    }

    // Set provisioned state on the responder
    bool setProvisioned(bool provisioned)
    {
        LOG_INFO("Setting provisioned state to: {}", provisioned);

        // Build SET_PROVISIONED request using serialization
        std::vector<uint8_t> request =
            spdm_serialization::createSetProvisionedRequest(0x12, provisioned);

        // Send request
        libspdm_return_t status = device_send_message(
            spdmContext, request.size(), request.data(), DEVICE_IO_TIMEOUT);
        if (status != LIBSPDM_STATUS_SUCCESS)
        {
            LOG_ERROR("Failed to send SET_PROVISIONED request");
            return false;
        }

        // Receive response
        std::array<uint8_t, LIBSPDM_MAX_SPDM_MSG_SIZE> buffer{};
        size_t responseSize = buffer.size();
        void* responsePtr = buffer.data();
        status = device_receive_message_all(spdmContext, &responseSize,
                                            &responsePtr, DEVICE_IO_TIMEOUT);
        if (status != LIBSPDM_STATUS_SUCCESS)
        {
            LOG_ERROR("Failed to receive SET_PROVISIONED response");
            return false;
        }

        // Deserialize and validate response
        try
        {
            std::vector<uint8_t> responseVec(buffer.begin(),
                                             buffer.begin() + responseSize);
            SpdmSetProvisionedResponse response =
                spdm_serialization::deserializeSetProvisionedResponse(
                    responseVec);

            if (response.header.request_response_code !=
                SPDM_SET_PROVISIONED_ACK)
            {
                LOG_ERROR("Invalid response code: 0x{:X}",
                          response.header.request_response_code);
                return false;
            }

            if (!response.status)
            {
                LOG_ERROR("SET_PROVISIONED failed with status: 0x{:X}",
                          response.status);
                return false;
            }

            LOG_INFO("Successfully set provisioned state to: {}", provisioned);
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to deserialize SET_PROVISIONED response: {}",
                      e.what());
            return false;
        }
    }

    std::string type_version()
    {
        return "1.1";
    }

    void* getSpdmContext()
    {
        return spdmContext;
    }

    // Static wrappers for device IO
    static libspdm_return_t device_send_message(
        void* spdm_context, size_t message_size, const void* message,
        uint64_t timeout)
    {
        auto ctx = static_cast<SpdmConnectionManager::AppContextData*>(
            fromContext(spdm_context));
        auto self = static_cast<SpdmConnectionManager*>(ctx->spdmConnection);
        return self->client.send(message, message_size, timeout)
                   ? LIBSPDM_STATUS_SUCCESS
                   : LIBSPDM_STATUS_SEND_FAIL;
    }

    static libspdm_return_t device_receive_message(
        void* spdm_context, size_t* message_size, void** message,
        uint64_t timeout)
    {
        auto ctx = static_cast<SpdmConnectionManager::AppContextData*>(
            fromContext(spdm_context));
        auto self = static_cast<SpdmConnectionManager*>(ctx->spdmConnection);
        return self->client.receive(*message, *message_size, timeout, false)
                   ? LIBSPDM_STATUS_SUCCESS
                   : LIBSPDM_STATUS_RECEIVE_FAIL;
    }

    static libspdm_return_t device_receive_message_all(
        void* spdm_context, size_t* message_size, void** message,
        uint64_t timeout)
    {
        auto ctx = static_cast<SpdmConnectionManager::AppContextData*>(
            fromContext(spdm_context));
        auto self = static_cast<SpdmConnectionManager*>(ctx->spdmConnection);
        return self->client.receive(*message, *message_size, timeout, false)
                   ? LIBSPDM_STATUS_SUCCESS
                   : LIBSPDM_STATUS_RECEIVE_FAIL;
    }

  private:
    SpdmTcpClient client;
};
