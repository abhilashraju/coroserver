#pragma once
/**
 * AsyncSpdmResponder — async counterpart of SpdmSession.
 *
 * Inherits SpdmConnection (context lifecycle, buffer callbacks, transport
 * registration) but does NOT register blocking device_send_message /
 * device_receive_message callbacks.  The async library function
 * libspdm_responder_dispatch_message_async accepts an AsyncSpdmTcpIO
 * reference directly and bypasses the callback mechanism entirely.
 *
 * Usage (from a coroutine spawned per accepted connection):
 *   auto session = std::make_shared<AsyncSpdmResponder>(ioCtx,
 *                                                        std::move(socket));
 *   co_spawn(ioCtx, session->asyncRun(), detached);
 */

#include "async_spdm_io.hpp"
#include "custom_message_dispatcher.hpp"
#include "logger.hpp"
// Note: responder_init.hpp is NOT included here — it pulls in cert_generator.hpp
// which chains to responder_object.hpp -> sdbusplus, requiring C++23.
// Responder initialization is handled by SpdmProtocolHandler::initializeResponder,
// which is included via spdm_protocol_handler.hpp.
#include "spdm_protocol_handler.hpp"
#include "spdmglobal.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>

extern "C"
{
#include <library/spdm_responder_lib.h>
}

#include "async/libspdm_rsp_dispatch_async.hpp"
#include "custom_message_handlers.hpp"

using boost::asio::ip::tcp;

/**
 * @brief Async SPDM responder session.
 *
 * One instance per accepted client connection.  Extends SpdmConnection for
 * context lifecycle management.  The dispatch loop uses
 * libspdm_responder_dispatch_message_async so the io_context never blocks.
 *
 * Custom message handling (vendor-defined messages) is preserved via the
 * existing CustomMessageDispatcher.
 */
class AsyncSpdmResponder : public SpdmConnection
{
  public:
    /**
     * @param ioCtx  The running io_context (shared by all sessions).
     * @param sock   Already-accepted tcp::socket for this client.
     */
    AsyncSpdmResponder(boost::asio::io_context& ioCtx, tcp::socket&& sock) :
        ioCtx_(ioCtx), io_(std::move(sock), ioCtx)
    {
        if (!protocolHandler_.initializeResponder(spdmContext))
        {
            throw std::runtime_error(
                "AsyncSpdmResponder: SPDM responder init failed");
        }

        // Register the libspdm application-level callback.
        // libspdm calls this when process_request cannot find a handler for
        // the opcode (our vendor opcodes 0x7D/0x7E/0x7F are unknown to it).
        // The decoded payload arrives here directly — no rawSend/rawReceive.
        libspdm_register_get_response_func(
            spdmContext,
            [](void* ctx, const uint32_t* /*session_id*/,
               bool /*is_app_message*/, size_t req_size, const void* req,
               size_t* rsp_size, void* rsp) -> libspdm_return_t {
                // Recover the AsyncSpdmResponder from the libspdm app context.
                auto* appCtx = SpdmConnection::fromContext(ctx);
                if (!appCtx || !appCtx->spdmConnection)
                    return LIBSPDM_STATUS_UNSUPPORTED_CAP;

                auto* self = static_cast<AsyncSpdmResponder*>(
                    appCtx->spdmConnection);

                const auto* begin = static_cast<const uint8_t*>(req);
                std::vector<uint8_t> reqVec(begin, begin + req_size);

                // Dispatch to the registered custom message handler.
                std::vector<uint8_t> respVec =
                    self->messageDispatcher_.dispatchToHandler(reqVec);

                if (respVec.empty())
                    return LIBSPDM_STATUS_UNSUPPORTED_CAP;

                if (respVec.size() > *rsp_size)
                    return LIBSPDM_STATUS_BUFFER_TOO_SMALL;

                std::memcpy(rsp, respVec.data(), respVec.size());
                *rsp_size = respVec.size();
                return LIBSPDM_STATUS_SUCCESS;
            });
    }

    AsyncSpdmResponder(const AsyncSpdmResponder&) = delete;
    AsyncSpdmResponder& operator=(const AsyncSpdmResponder&) = delete;
    AsyncSpdmResponder(AsyncSpdmResponder&&) = delete;
    AsyncSpdmResponder& operator=(AsyncSpdmResponder&&) = delete;

    ~AsyncSpdmResponder()
    {
        io_.close();
    }

    /**
     * @brief Register custom (vendor-defined) message handlers.
     */
    void setCustomMessageHandlers(
        const std::map<uint8_t, CustomMessageHandler>& handlers)
    {
        messageDispatcher_.setCustomMessageHandlers(handlers);
    }

    void addCustomMessageHandler(uint8_t messageCode,
                                 CustomMessageHandler handler)
    {
        messageDispatcher_.addCustomMessageHandler(messageCode, handler);
    }

    /**
     * @brief Main session coroutine.
     *
     * Loops calling libspdm_responder_dispatch_message_async until the
     * connection terminates.  Mirrors all status-code handling from
     * SpdmSession::startDispatch.
     *
     * Suitable for: co_spawn(ioCtx, session->asyncRun(), detached)
     */
    boost::asio::awaitable<void> asyncRun()
    {
        size_t retryCount = 0;
        static constexpr size_t maxRetries = 100;

        while (true)
        {
            libspdm_return_t status =
                co_await libspdm_responder_dispatch_message_async(spdmContext,
                                                                   io_);

            if (status == LIBSPDM_STATUS_SUCCESS)
            {
                retryCount = 0;
                continue;
            }

            if (status == LIBSPDM_STATUS_SEND_FAIL)
            {
                LOG_ERROR("AsyncSpdmResponder: send failure — closing session");
                break;
            }

            if (status == LIBSPDM_STATUS_RECEIVE_FAIL)
            {
                // Receive failure is usually a clean peer disconnect.
                LOG_INFO(
                    "AsyncSpdmResponder: peer disconnected — closing session");
                break;
            }

            if (status == LIBSPDM_STATUS_INVALID_MSG_FIELD)
            {
                if (++retryCount >= maxRetries)
                {
                    LOG_ERROR(
                        "AsyncSpdmResponder: max retries on INVALID_MSG_FIELD");
                    break;
                }
                continue;
            }

            // Unexpected status
            LOG_ERROR("AsyncSpdmResponder: unexpected status 0x{:x}",
                      static_cast<uint32_t>(status));
            if (++retryCount >= maxRetries)
            {
                break;
            }
        }

        LOG_INFO("AsyncSpdmResponder: session ended");
        co_return;
    }

    tcp::socket& getSocket()
    {
        return *io_.socket;
    }

  private:
    boost::asio::io_context& ioCtx_;
    AsyncSpdmTcpIO io_;
    SpdmProtocolHandler protocolHandler_;
    CustomMessageDispatcher messageDispatcher_;
};
