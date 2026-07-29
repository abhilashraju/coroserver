#pragma once
/**
 * AsyncSpdmTcpServer — async counterpart of SpdmTcpServer.
 *
 * Accepts TCP connections and spawns one AsyncSpdmResponder coroutine per
 * client.  All sessions run concurrently on the same io_context thread;
 * no session blocks any other.
 */

#include "async_spdm_responder.hpp"
#include "custom_message_dispatcher.hpp"
#include "logger.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <map>
#include <memory>
#include <set>

using boost::asio::ip::tcp;

/**
 * @brief Async TCP server that spawns an AsyncSpdmResponder per connection.
 *
 * An optional map of custom message handlers can be supplied at construction
 * time; they are injected into every new session via
 * AsyncSpdmResponder::setCustomMessageHandlers so vendor-defined messages are
 * handled the same way as in the synchronous SpdmResponder.
 *
 * Usage:
 *   AsyncSpdmTcpServer server(io, port, handlers);
 *   server.accept();
 *   io.run();
 */
class AsyncSpdmTcpServer
{
  public:
    AsyncSpdmTcpServer(
        boost::asio::io_context& ioCtx, uint16_t port,
        std::map<uint8_t, CustomMessageHandler> handlers = {},
        size_t maxSessions = 10) :
        ioCtx_(ioCtx),
        acceptor_(ioCtx, tcp::endpoint(tcp::v4(), port)),
        handlers_(std::move(handlers)), maxSessions_(maxSessions)
    {}

    /**
     * @brief Start the accept loop.
     *
     * Non-blocking: posts async_accept onto the io_context; returns immediately.
     * Call io_context.run() to drive the event loop.
     */
    void accept()
    {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec)
                {
                    if (activeSessions_.size() >= maxSessions_)
                    {
                        LOG_WARNING(
                            "AsyncSpdmTcpServer: max sessions ({}) reached — "
                            "rejecting connection",
                            maxSessions_);
                        socket.close();
                    }
                    else
                    {
                        auto session = std::make_shared<AsyncSpdmResponder>(
                            ioCtx_, std::move(socket));
                        if (!handlers_.empty())
                        {
                            session->setCustomMessageHandlers(handlers_);
                        }
                        activeSessions_.insert(session);

                        boost::asio::co_spawn(
                            ioCtx_,
                            [this, session]() -> boost::asio::awaitable<void> {
                                co_await session->asyncRun();
                                activeSessions_.erase(session);
                            },
                            boost::asio::detached);
                    }
                }
                else
                {
                    LOG_ERROR("AsyncSpdmTcpServer: accept error: {}",
                              ec.message());
                }
                // Continue accepting next connection
                accept();
            });
    }

  private:
    boost::asio::io_context& ioCtx_;
    tcp::acceptor acceptor_;
    std::map<uint8_t, CustomMessageHandler> handlers_;
    size_t maxSessions_;
    std::set<std::shared_ptr<AsyncSpdmResponder>> activeSessions_;
};
