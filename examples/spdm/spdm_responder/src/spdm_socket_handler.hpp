#pragma once
#include "logger.hpp"
#include "socket_functions.hpp"

#include <boost/asio.hpp>

#include <cstdint>
#include <vector>

// Socket timeout in milliseconds (10 seconds)
constexpr uint64_t SOCKET_TIMEOUT_MS = 10000;

/**
 * @brief Handles socket I/O operations for SPDM communication
 *
 * This class is responsible for all socket-level operations including
 * sending, receiving, and managing socket lifecycle.
 */
class SpdmSocketHandler
{
  public:
    explicit SpdmSocketHandler(boost::asio::ip::tcp::socket&& sock) :
        socket_(std::move(sock))
    {
        configure_socket_timeout(socket_, SOCKET_TIMEOUT_MS);
    }

    ~SpdmSocketHandler()
    {
        close();
    }

    bool send(const void* data, size_t& size, uint64_t timeout_ms)
    {
        return write_with_timeout(socket_, data, size, timeout_ms);
    }

    bool receive(void* data, size_t& size, uint64_t timeout_ms,
                 bool strict = false)
    {
        return read_all_with_timeout(socket_, data, size, timeout_ms, strict);
    }

    void close()
    {
        boost::system::error_code ec;
        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }

    boost::asio::ip::tcp::socket& getSocket()
    {
        return socket_;
    }

  private:
    boost::asio::ip::tcp::socket socket_;
};
