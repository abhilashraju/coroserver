#pragma once
/**
 * Socket-backed implementation of the AsyncSpdmIO concept.
 *
 * AsyncSpdmTcpIO wraps a shared tcp::socket + steady_timer and forwards all
 * I/O calls to the async_spdm_send / async_spdm_receive free functions from
 * async_socket_functions.hpp.
 *
 * Usage:
 *   auto io = makeAsyncSpdmIO(std::move(acceptedSocket), ioContext);
 *   co_await libspdm_responder_dispatch_message_async(ctx, io);
 */

#include "async_socket_functions.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <memory>

using boost::asio::ip::tcp;

/**
 * @brief Concrete AsyncSpdmIO backed by a plain TCP socket.
 *
 * Owns a shared_ptr<tcp::socket> and shared_ptr<steady_timer> so it can be
 * copied cheaply and the socket lifetime tracked through RAII.
 */
struct AsyncSpdmTcpIO
{
    std::shared_ptr<tcp::socket> socket;
    std::shared_ptr<boost::asio::steady_timer> timer;

    /**
     * @brief Construct from an already-connected / already-accepted socket.
     *
     * The socket is moved into a shared_ptr so multiple I/O operations can
     * share lifetime tracking.
     *
     * @param sock    TCP socket (connected or accepted).
     * @param ioCtx   io_context used to construct the steady_timer.
     */
    AsyncSpdmTcpIO(tcp::socket&& sock, boost::asio::io_context& ioCtx) :
        socket(std::make_shared<tcp::socket>(std::move(sock))),
        timer(std::make_shared<boost::asio::steady_timer>(ioCtx))
    {}

    /** Move-only (shared_ptrs are copyable, but intent is move). */
    AsyncSpdmTcpIO(const AsyncSpdmTcpIO&) = default;
    AsyncSpdmTcpIO& operator=(const AsyncSpdmTcpIO&) = default;
    AsyncSpdmTcpIO(AsyncSpdmTcpIO&&) = default;
    AsyncSpdmTcpIO& operator=(AsyncSpdmTcpIO&&) = default;

    /** Send exactly `size` bytes.  Returns true on success. */
    boost::asio::awaitable<bool> send(const void* data, std::size_t size,
                                      uint64_t timeout_us)
    {
        co_return co_await async_spdm_send(socket, timer, data, size,
                                           timeout_us);
    }

    /** Receive up to `size` bytes; updates `size` with actual count. */
    boost::asio::awaitable<bool> receive(void* data, std::size_t& size,
                                         uint64_t timeout_us)
    {
        co_return co_await async_spdm_receive(socket, timer, data, size,
                                              timeout_us);
    }

    /** Close the underlying socket. */
    void close()
    {
        if (socket && socket->is_open())
        {
            boost::system::error_code ec;
            socket->shutdown(tcp::socket::shutdown_both, ec);
            socket->close(ec);
        }
    }

    bool isOpen() const
    {
        return socket && socket->is_open();
    }
};

/**
 * @brief Factory helper.
 *
 * @param sock   An already-connected or already-accepted tcp::socket.
 * @param ioCtx  The running io_context for timer construction.
 */
inline AsyncSpdmTcpIO makeAsyncSpdmIO(tcp::socket&& sock,
                                       boost::asio::io_context& ioCtx)
{
    return AsyncSpdmTcpIO(std::move(sock), ioCtx);
}
