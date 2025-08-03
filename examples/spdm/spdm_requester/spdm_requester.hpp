#pragma once
#include "logger.hpp"
#include "requester_init.hpp"
#include "spdm_requester.hpp"
#include "spdm_tcp_client.hpp"
#include "spdmglobal.hpp"

#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

struct SpdmRequester : SpdmConnectionTemplate<SpdmRequester>
{
  public:
    SpdmRequester(SpdmTcpClient& client) : client(client)
    {
        libspdm_data_parameter_t parameter = {};
        parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
        uint8_t ct_exponent = 0;
        uint32_t cap_flags = 0;
        libspdm_set_data(spdmContext, LIBSPDM_DATA_CAPABILITY_CT_EXPONENT,
                         &parameter, &ct_exponent, sizeof(ct_exponent));
        libspdm_set_data(spdmContext, LIBSPDM_DATA_CAPABILITY_FLAGS, &parameter,
                         &cap_flags, sizeof(cap_flags));
        // ... set other algorithms as needed
    }

    ~SpdmRequester() {}

    bool connect(const std::string& host, uint16_t port)
    {
        return !client.connect(host, port);
    }
    bool sayhello()
    {
        // constexpr uint64_t timeout = 5 * 1000 * 1000;
        // size_t size = sizeof("Hello");
        // if (client.send("Hello", size, timeout))
        // {
        //     std::array<char, 50> buff{0};
        //     size = sizeof("Hello");
        //     return client.receive(buff.data(), size, timeout);
        // }
        return true;
    }
    bool init_connection()
    {
        // Initialize connection (GET_VERSION, GET_CAPABILITIES,
        //  NEGOTIATE_ALGORITHMS)
        return spdmClientInit(spdmContext);
    }

    // Static wrappers for device IO
    static libspdm_return_t device_send_message(
        void* spdm_context, size_t message_size, const void* message,
        uint64_t timeout)
    {
        auto ctx = static_cast<SpdmRequester::AppContextData*>(
            fromContext(spdm_context));
        auto self = static_cast<SpdmRequester*>(ctx->spdmConnection);
        return self->client.send(message, message_size, timeout)
                   ? LIBSPDM_STATUS_SUCCESS
                   : LIBSPDM_STATUS_SEND_FAIL;
    }
    static libspdm_return_t device_receive_message(
        void* spdm_context, size_t* message_size, void** message,
        uint64_t timeout)
    {
        auto ctx = static_cast<SpdmRequester::AppContextData*>(
            fromContext(spdm_context));
        auto self = static_cast<SpdmRequester*>(ctx->spdmConnection);
        return self->client.receive(*message, *message_size, timeout)
                   ? LIBSPDM_STATUS_SUCCESS
                   : LIBSPDM_STATUS_RECEIVE_FAIL;
    }

  private:
    SpdmTcpClient& client;
};
