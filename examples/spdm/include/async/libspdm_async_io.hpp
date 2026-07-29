#pragma once
/**
 * AsyncSpdmIO concept / base type used by the async requester and responder
 * libraries.  Any concrete type that provides these two coroutine methods can
 * be used as the I/O back-end.
 *
 * The send/receive functions mirror the libspdm callback signatures:
 *  - data / size: raw transport-layer buffer and its byte count.
 *  - timeout_us : timeout in microseconds (matches libspdm's `uint64_t timeout`
 *                 parameter; 0 means "use a reasonable default").
 */

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>

/**
 * @brief Concept that any async I/O provider must satisfy.
 *
 * A conforming type T must provide:
 *   boost::asio::awaitable<bool> send(const void*, size_t, uint64_t);
 *   boost::asio::awaitable<bool> receive(void*, size_t&, uint64_t);
 */
template <typename T>
concept AsyncSpdmIO = requires(T io, const void* cdata, void* mdata,
                               std::size_t sz, uint64_t timeout) {
    {
        io.send(cdata, sz, timeout)
    } -> std::same_as<boost::asio::awaitable<bool>>;
    {
        io.receive(mdata, sz, timeout)
    } -> std::same_as<boost::asio::awaitable<bool>>;
};
