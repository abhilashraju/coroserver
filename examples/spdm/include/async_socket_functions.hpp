#pragma once
#include "beastdefs.hpp"
#include "logger.hpp"
#include "socket_streams.hpp"

#include <boost/asio.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

using boost::asio::ip::tcp;

/**
 * @brief Write exactly `n` bytes using TimedStreamer, looping over write()
 *        to handle short writes from async_write_some.
 */
inline boost::asio::awaitable<bool>
    streamer_write_exact(reactor::TimedStreamer<tcp::socket>& streamer,
                         const void* data, std::size_t n)
{
    const auto* buf = static_cast<const uint8_t*>(data);
    std::size_t total = 0;
    while (total < n)
    {
        auto [ec, written] = co_await streamer.write(
            boost::asio::const_buffer(buf + total, n - total));
        if (ec)
        {
            LOG_ERROR("async_spdm write error after {}/{} bytes: {}",
                      total, n, ec.message());
            co_return false;
        }
        if (written == 0)
        {
            LOG_ERROR("async_spdm write returned 0 after {}/{} bytes", total, n);
            co_return false;
        }
        total += written;
    }
    co_return true;
}

/**
 * @brief Read exactly `n` bytes using TimedStreamer, looping over read()
 *        to handle short reads from async_read_some.
 */
inline boost::asio::awaitable<bool>
    streamer_read_exact(reactor::TimedStreamer<tcp::socket>& streamer,
                        void* data, std::size_t n, bool timeout = true)
{
    auto* buf = static_cast<uint8_t*>(data);
    std::size_t total = 0;
    while (total < n)
    {
        auto [ec, got] = co_await streamer.read(
            boost::asio::mutable_buffer(buf + total, n - total), timeout);
        if (ec)
        {
            LOG_ERROR("async_spdm read error after {}/{} bytes: {}",
                      total, n, ec.message());
            co_return false;
        }
        if (got == 0)
        {
            LOG_ERROR("async_spdm EOF after {}/{} bytes", total, n);
            co_return false;
        }
        total += got;
    }
    co_return true;
}

/**
 * @brief Async write: sends the transport-encoded SPDM message verbatim.
 *
 * libspdm_tcp_encode_message has already prepended the spdm_tcp_binding_header_t
 * into the buffer before this is called.  Just write all `size` bytes as-is.
 *
 * Uses TimedStreamer for cancellation-on-timeout.
 * Timeout is in microseconds to match libspdm's `timeout` parameter.
 */
inline boost::asio::awaitable<bool>
    async_spdm_send(std::shared_ptr<tcp::socket> socket,
                    std::shared_ptr<boost::asio::steady_timer> timer,
                    const void* data, std::size_t size, uint64_t timeout_us)
{
    using namespace reactor;
    if (size == 0)
        co_return true;

    TimedStreamer<tcp::socket> streamer(socket, timer);
    streamer.setTimeout(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::microseconds(timeout_us > 0 ? timeout_us : 10'000'000ULL)));

    LOG_DEBUG("async_spdm_send: {} bytes", size);
    if (!co_await streamer_write_exact(streamer, data, size))
    {
        LOG_ERROR("async_spdm_send: failed");
        co_return false;
    }
    co_return true;
}

/**
 * @brief Async receive: reads one complete SPDM TCP transport message.
 *
 * The spdm_tcp_binding_header_t (4 bytes) at the start of every message
 * contains a payload_length field:
 *   payload_length = (total message size) - 2
 * so total = payload_length + 2.
 *
 * We read the 4-byte header first, compute total size, then read the rest.
 * The caller (libspdm_process_request / transport_decode_message) uses the
 * payload_length field from inside the buffer — it does not need `size` updated.
 *
 * Uses TimedStreamer for cancellation-on-timeout.
 * Timeout is in microseconds to match libspdm's `timeout` parameter.
 */
inline boost::asio::awaitable<bool>
    async_spdm_receive(std::shared_ptr<tcp::socket> socket,
                       std::shared_ptr<boost::asio::steady_timer> timer,
                       void* data, std::size_t& size, uint64_t timeout_us)
{
    using namespace reactor;

    TimedStreamer<tcp::socket> streamer(socket, timer);
    auto* buf = static_cast<uint8_t*>(data);

    // Read the 4-byte header with NO timeout — pass timeout=false so the
    // shared timer is never armed here.  The requester may be idle for an
    // arbitrary duration between SPDM exchanges.
    if (!co_await streamer_read_exact(streamer, buf, 4, /*timeout=*/false))
    {
        LOG_ERROR("async_spdm_receive: failed to read header");
        co_return false;
    }

    // Header arrived — now arm the timeout only for the payload read.
    streamer.setTimeout(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::microseconds(timeout_us > 0 ? timeout_us : 10'000'000ULL)));

    // payload_length (LE uint16_t at offset 0) = total_size - 2
    const uint16_t payload_length =
        static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
    const std::size_t total_size = static_cast<std::size_t>(payload_length) + 2;

    LOG_DEBUG("async_spdm_receive: total_size={}", total_size);

    if (total_size < 4)
    {
        // payload_length=0 (all-zero header) means the peer closed the
        // connection or sent a keep-alive/FIN frame with no payload.
        // Treat as clean disconnect — not a protocol error.
        LOG_INFO("async_spdm_receive: peer disconnected (total_size={})",
                 total_size);
        co_return false;
    }

    if (total_size > size)
    {
        LOG_ERROR("async_spdm_receive: total_size={} exceeds capacity={}",
                  total_size, size);
        co_return false;
    }

    // Read the remaining bytes after the header
    if (!co_await streamer_read_exact(streamer, buf + 4, total_size - 4))
    {
        LOG_ERROR("async_spdm_receive: failed to read payload");
        co_return false;
    }

    co_return true;
}
