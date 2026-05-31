#pragma once
#include "custom_message_dispatcher.hpp"
#include "logger.hpp"
#include "spdm_measurement_utils.hpp"
#include "spdm_protocol_handler.hpp"
#include "spdm_socket_handler.hpp"

#include <boost/asio.hpp>

#include <cstdint>
#include <memory>
#include <set>
#include <vector>
extern "C"
{
#include <library/spdm_responder_lib.h>
#include <library/spdm_transport_tcp_lib.h>
}
#include "cert_exchange_handler.hpp"
#include "custom_message_handlers.hpp"
#include "responder_object.hpp"
#include "spdmglobal.hpp"

/**
 * @brief SPDM session handler for managing individual client connections
 *
 * This class represents a single SPDM (Security Protocol and Data Model)
 * session with a client. It manages the socket connection, protocol handling,
 * and message dispatching for SPDM communication.
 *
 * @details
 * Lifecycle:
 * - Created when a client connects to the SpdmTcpServer
 * - Runs in its own coroutine/thread context via boost::asio
 * - Automatically cleaned up when the session ends or client disconnects
 *
 * Thread Safety:
 * - NOT thread-safe: Each session should be accessed only from its own
 * execution context
 * - The io_context ensures serialized access to session operations
 *
 * Ownership:
 * - Managed via std::shared_ptr by SpdmTcpServer
 * - Session lifetime tied to active connection and server's activeSessions set
 *
 * Usage Example:
 * @code
 * auto session = std::make_shared<SpdmSession>(ioContext, std::move(socket));
 * session->addCustomMessageHandler(0x42, myCustomHandler);
 * session->run(); // Blocks until session completes
 * @endcode
 */
struct SpdmSession : public SpdmConnectionTemplate<SpdmSession>
{
    boost::asio::io_context& ioContext;
    SpdmSocketHandler socketHandler;
    SpdmProtocolHandler protocolHandler;
    CustomMessageDispatcher messageDispatcher;
    std::vector<uint8_t> lastReceivedMessage;

    SpdmSession(boost::asio::io_context& ioCtx,
                boost::asio::ip::tcp::socket&& sock) :
        ioContext(ioCtx), socketHandler(std::move(sock))
    {
        if (!protocolHandler.initializeResponder(spdmContext))
        {
            LOG_ERROR(
                "Failed to initialize SPDM responder: certificate chain loading failed");
            throw std::runtime_error("SPDM responder initialization failed");
        }

        // Set up the send callback for custom message dispatcher
        messageDispatcher.setSendCallback(
            [this](const void* data, size_t size) -> bool {
                constexpr uint64_t DEVICE_IO_TIMEOUT = 10000000;
                size_t sendSize = size;
                return socketHandler.send(data, sendSize, DEVICE_IO_TIMEOUT);
            });
    }
    ~SpdmSession()
    {
        socketHandler.close();
    }

    boost::asio::ip::tcp::socket& stream()
    {
        return socketHandler.getSocket();
    }

    bool send(const void* data, size_t& size, uint64_t timeout_ms)
    {
        return socketHandler.send(data, size, timeout_ms);
    }

    bool receive(void* data, size_t& size, uint64_t timeout_ms,
                 bool strict = false)
    {
        bool result = socketHandler.receive(data, size, timeout_ms, strict);
        // Message caching removed - will be done on-demand when needed
        return result;
    }

    // Cache the last received message for custom message handling
    void cacheLastMessage(const void* data, size_t size)
    {
        lastReceivedMessage.assign(static_cast<const uint8_t*>(data),
                                   static_cast<const uint8_t*>(data) + size);
    }

    void close()
    {
        socketHandler.close();
    }
    void setCustomMessageHandlers(
        const std::map<uint8_t, CustomMessageHandler>& handlers)
    {
        messageDispatcher.setCustomMessageHandlers(handlers);
    }

    // Add a single custom message handler
    void addCustomMessageHandler(uint8_t messageCode,
                                 CustomMessageHandler handler)
    {
        messageDispatcher.addCustomMessageHandler(messageCode, handler);
    }

    bool handleCustomMessage()
    {
        return messageDispatcher.handleCustomMessage(spdmContext,
                                                     lastReceivedMessage);
    }

    // Helper method for testing responder-side measurement functionality
    // Note: This is not part of the normal SPDM protocol flow

    bool receiveHello()
    {
        return true;
    }
    // Helper function to log SPDM message types
    static void logSpdmMessageType(const void* message, size_t message_size)
    {
        SpdmProtocolHandler::logSpdmMessageType(message, message_size);
    }

    // Device IO send/receive wrappers for libspdm
    static libspdm_return_t device_send_message(
        void* spdm_context, size_t message_size, const void* message,
        uint64_t timeout)
    {
        auto ctx = static_cast<SpdmSession::AppContextData*>(
            fromContext(spdm_context));
        auto self = static_cast<SpdmSession*>(ctx->spdmConnection);
        return self->send(message, message_size, timeout)
                   ? LIBSPDM_STATUS_SUCCESS
                   : LIBSPDM_STATUS_SEND_FAIL;
    }
    static libspdm_return_t device_receive_message(
        void* spdm_context, size_t* message_size, void** message,
        uint64_t timeout)
    {
        auto ctx = static_cast<SpdmSession::AppContextData*>(
            fromContext(spdm_context));
        auto self = static_cast<SpdmSession*>(ctx->spdmConnection);
        bool result = self->receive(*message, *message_size, timeout);

        // Log the message type after successful receive
        if (result && *message && *message_size > 0)
        {
            logSpdmMessageType(*message, *message_size);
            // Cache message for potential custom message handling
            self->cacheLastMessage(*message, *message_size);
        }

        return result ? LIBSPDM_STATUS_SUCCESS : LIBSPDM_STATUS_RECEIVE_FAIL;
    }
    static void spawn(std::shared_ptr<SpdmSession> session)
    {
        session->run();
    }
    void run()
    {
        if (receiveHello())
        {
            bool continue_serving = true;
            while (continue_serving)
            {
                continue_serving = startDispatch();
            }
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
                protocolHandler.resetRetryCount();
            }
            else if ((status == LIBSPDM_STATUS_SEND_FAIL))
            {
                LOG_ERROR("Server Critical Error - STOP");
                return false;
            }
            else if (status == LIBSPDM_STATUS_RECEIVE_FAIL)
            {
                LOG_ERROR("Receive failed - terminating session");
                return false;
            }
            else if (status == LIBSPDM_STATUS_UNSUPPORTED_CAP)
            {
                // Try to handle custom messages
                if (!handleCustomMessage())
                {
                    if (!protocolHandler.checkRetryLimit(
                            "Max retries reached for unsupported capability"))
                    {
                        return false;
                    }
                    continue;
                }
                protocolHandler.resetRetryCount();
            }
            else if (status == LIBSPDM_STATUS_INVALID_MSG_FIELD)
            {
                // Invalid message field - ignore and continue
                if (!protocolHandler.checkRetryLimit(
                        "Max retries reached for invalid message fields"))
                {
                    return false;
                }
                continue;
            }
            else
            {
                // Handle unexpected status codes
                LOG_ERROR("Unexpected libspdm status code: 0x%x", status);
                if (!protocolHandler.checkRetryLimit(
                        "Max retries reached for unexpected errors"))
                {
                    return false;
                }
                continue;
            }
        }
    }
};
/**
 * @brief TCP server for accepting and managing SPDM client connections
 *
 * This class implements a TCP server that listens for incoming SPDM client
 * connections and manages multiple concurrent sessions. It uses boost::asio
 * for asynchronous I/O operations.
 *
 * @details
 * Lifecycle:
 * - Constructed with io_context and port number
 * - Call accept() to start listening for connections
 * - Runs until io_context is stopped or server is destroyed
 *
 * Thread Safety:
 * - NOT thread-safe: Should be accessed only from the io_context thread
 * - All async operations are serialized by the io_context
 *
 * Ownership:
 * - Owns the acceptor and manages session lifecycle
 * - Sessions are stored as shared_ptr in activeSessions set
 * - Automatically cleans up sessions when they complete
 *
 * Usage Example:
 * @code
 * boost::asio::io_context ioContext;
 * SpdmTcpServer server(ioContext, 2323, 10); // port 2323, max 10 sessions
 *
 * server.setSessionHandler([](auto session) -> boost::asio::awaitable<void> {
 *     session->run();
 *     co_return;
 * });
 *
 * server.accept(); // Start accepting connections
 * ioContext.run(); // Run event loop
 * @endcode
 */
class SpdmTcpServer
{
  public:
    SpdmTcpServer(boost::asio::io_context& ioCtx, uint16_t port,
                  size_t maxSessions = 10) :
        ioContext(ioCtx),
        acceptor(ioCtx, boost::asio::ip::tcp::endpoint(
                            boost::asio::ip::tcp::v4(), port)),
        maxConcurrentSessions(maxSessions)
    {}
    void accept()
    {
        acceptor.async_accept([this](boost::system::error_code ec,
                                     boost::asio::ip::tcp::socket socket) {
            if (!ec)
            {
                if (activeSessions.size() >= maxConcurrentSessions)
                {
                    LOG_WARNING("Maximum concurrent sessions ({}) reached - "
                                "rejecting new connection",
                                maxConcurrentSessions);
                    socket.close();
                }
                else
                {
                    auto session = std::make_shared<SpdmSession>(
                        ioContext, std::move(socket));
                    activeSessions.insert(session);
                    if (sessionHandler)
                    {
                        boost::asio::co_spawn(
                            ioContext,
                            [this, session]() -> boost::asio::awaitable<void> {
                                co_await sessionHandler(session);
                                activeSessions.erase(session);
                            },
                            boost::asio::detached);
                    }
                    else
                    {
                        LOG_ERROR("Session handler not set - closing session");
                        activeSessions.erase(session);
                    }
                }
            }
            accept();
        });
    }
    void setSessionHandler(std::function<boost::asio::awaitable<void>(
                               std::shared_ptr<SpdmSession>)>
                               handler)
    {
        sessionHandler = handler;
    }

  private:
    boost::asio::io_context& ioContext;
    boost::asio::ip::tcp::acceptor acceptor;
    std::function<boost::asio::awaitable<void>(std::shared_ptr<SpdmSession>)>
        sessionHandler;
    size_t maxConcurrentSessions;
    std::set<std::shared_ptr<SpdmSession>> activeSessions;
};
