#include "logger.hpp"

#include <sys/socket.h>
#include <sys/time.h>

#include <boost/asio.hpp>

#include <chrono>
#include <iostream>
using boost::asio::ip::tcp;

/**
 * @brief Configure socket timeout options at socket creation time
 *
 * @param socket TCP socket to configure
 * @param timeout_ms Timeout in milliseconds for both send and receive
 * operations
 */
inline void configure_socket_timeout(tcp::socket& socket, uint64_t timeout_ms)
{
    // Convert milliseconds to timeval structure
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    // Get native socket handle
    auto native_sock = socket.native_handle();

    // Set socket receive timeout using setsockopt
    if (setsockopt(native_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        LOG_ERROR("Failed to set SO_RCVTIMEO");
    }

    // Set socket send timeout using setsockopt
    if (setsockopt(native_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0)
    {
        LOG_ERROR("Failed to set SO_SNDTIMEO");
    }
}

bool write_with_timeout(tcp::socket& socket, const void* data,
                        std::size_t& size, uint64_t timeout_ms)
{
    boost::system::error_code ec;
    boost::asio::streambuf buf;
    std::ostream os(&buf);
    os.write(static_cast<const char*>(data), size);
    os.flush();
    buf.commit(size);
    size = boost::asio::write(socket, buf, ec);
    if (ec)
    {
        LOG_ERROR("Write error: {} ", ec.message());
        return false;
    }
    return true;
}
bool read_with_timeout(tcp::socket& socket, void* data, std::size_t& size,
                       uint64_t timeout_ms = 0)
{
    if (timeout_ms > 0)
    {
        // Configure socket timeout options at socket creation time
        configure_socket_timeout(socket, timeout_ms);
    }
    boost::system::error_code ec;
    boost::asio::streambuf buf;
    std::istream is(&buf);
    std::size_t n = boost::asio::read(socket, buf.prepare(size),
                                      boost::asio::transfer_at_least(1), ec);
    if (ec)
    {
        LOG_ERROR("Read error: {} ", ec.message());
        return false;
    }
    if (n > 0)
    {
        buf.commit(n);
        is.read(static_cast<char*>(data), n);
    }
    size = n;
    return true;
}

/**
 * @brief Read all bytes from socket until the specified size is reached or
 * timeout occurs
 *
 * @param socket TCP socket to read from
 * @param data Buffer to store the read data
 * @param size Input: expected size to read, Output: actual size read
 * @param timeout_ms Timeout in milliseconds
 * @return true if all bytes were read successfully, false otherwise
 *
 * If the function returns false, the data buffer will contain the bytes read so
 * far and size will be updated with the actual number of bytes read.
 */
bool read_all_with_timeout(tcp::socket& socket, void* data, std::size_t& size,
                           uint64_t timeout_ms = 0, bool strict = false)
{
    if (size == 0)
    {
        return true;
    }
    if (!strict)
    {
        return read_with_timeout(socket, data, size, timeout_ms);
    }

    std::size_t expected_size = size;
    char* buffer = static_cast<char*>(data);
    std::size_t total_bytes_read = 0;

    // Read in a loop until we get all expected bytes
    while (total_bytes_read < expected_size)
    {
        std::size_t remaining = expected_size - total_bytes_read;
        std::size_t chunk_size = remaining;

        // Call read_with_timeout to read the remaining bytes
        if (!read_with_timeout(socket, buffer + total_bytes_read, chunk_size,
                               timeout_ms))
        {
            // Update size with actual bytes read before returning
            size = total_bytes_read;
            LOG_ERROR("Read failed: read {} of {} bytes", total_bytes_read,
                      expected_size);
            return false;
        }

        total_bytes_read += chunk_size;
    }

    size = total_bytes_read;
    return total_bytes_read == expected_size;
}
