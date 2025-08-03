#pragma once

#include "spdm_tcp_server.hpp"
extern "C"
{
#include <libspdm/include/library/spdm_responder_lib.h>
#include <libspdm/include/library/spdm_transport_tcp_lib.h>
}
#include "responder_init.hpp"
#include "spdmglobal.hpp"

#include <boost/asio.hpp>
struct SpdmResponder : public SpdmConnectionTemplate<SpdmResponder>
{
  public:
    SpdmResponder(SpdmTcpServer& server) : server(server)
    {
        spdmResponderInit(spdmContext);
    }

    void run()
    {
        bool continue_serving = true;
        while (continue_serving)
        {
            if (!server.accept())
                continue;
            if (receiveHello())
            {
                continue_serving = startDispatch();
            }
            server.close();
        }
    }
    bool startDispatch()
    {
        while (true)
        {
            libspdm_return_t status =
                libspdm_responder_dispatch_message(spdmContext);
            if (status == LIBSPDM_STATUS_SUCCESS)
            {
                /* success dispatch SPDM message*/
            }
            if ((status == LIBSPDM_STATUS_SEND_FAIL) ||
                (status == LIBSPDM_STATUS_RECEIVE_FAIL))
            {
                printf("Server Critical Error - STOP\n");
                return false;
            }
            if (status != LIBSPDM_STATUS_UNSUPPORTED_CAP)
            {
                continue;
            }
        }
    }
    bool receiveHello()
    {
        // std::array<char, 50> buff{0};
        // constexpr uint64_t timeout = 0;
        // size_t size = sizeof("Hello");
        // if (server.receive(buff.data(), size, timeout))
        // {
        //     size = sizeof("Hello");
        //     return server.send("Hello", size, timeout);
        // }
        return true;
    }

    ~SpdmResponder() {}

    // Device IO send/receive wrappers for libspdm
    static libspdm_return_t device_send_message(
        void* spdm_context, size_t message_size, const void* message,
        uint64_t timeout)
    {
        auto ctx = static_cast<SpdmResponder::AppContextData*>(
            fromContext(spdm_context));
        auto self = static_cast<SpdmResponder*>(ctx->spdmConnection);
        return self->server.send(message, message_size, timeout)
                   ? LIBSPDM_STATUS_SUCCESS
                   : LIBSPDM_STATUS_SEND_FAIL;
    }
    static libspdm_return_t device_receive_message(
        void* spdm_context, size_t* message_size, void** message,
        uint64_t timeout)
    {
        auto ctx = static_cast<SpdmResponder::AppContextData*>(
            fromContext(spdm_context));
        auto self = static_cast<SpdmResponder*>(ctx->spdmConnection);
        return self->server.receive(*message, *message_size, timeout)
                   ? LIBSPDM_STATUS_SUCCESS
                   : LIBSPDM_STATUS_RECEIVE_FAIL;
    }

    SpdmTcpServer& server;
};
