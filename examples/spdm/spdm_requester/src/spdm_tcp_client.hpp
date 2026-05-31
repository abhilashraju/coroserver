#pragma once
#include "logger.hpp"
#include "socket_functions.hpp"

#include <boost/asio.hpp>

// Default timeout for socket operations (10 seconds in milliseconds)
constexpr uint64_t DEFAULT_SOCKET_TIMEOUT_MS = 10000;

class SpdmTcpClient
{
  public:
    SpdmTcpClient(boost::asio::io_context& io_context) :
        ioContext(io_context), resolver(io_context), socket(io_context)
    {}
    boost::system::error_code connect(
        const std::string& host, uint16_t port,
        uint64_t timeout_ms = DEFAULT_SOCKET_TIMEOUT_MS)
    {
        boost::system::error_code ec;
        auto endpoints = resolver.resolve(host, std::to_string(port), ec);
        if (ec)
        {
            LOG_ERROR("Error resolving {}:{}. Error: {}", host, port,
                      ec.message());
            return ec;
        }
        boost::asio::connect(socket, endpoints, ec);
        if (ec)
        {
            LOG_ERROR("Error connecting to {}:{}. Error: {}", host, port,
                      ec.message());
            return ec;
        }

        // Configure socket timeout options at connection time
        configure_socket_timeout(socket, timeout_ms);

        return ec;
    }
    bool send(const void* data, size_t size, uint64_t timeout_ms)
    {
        return write_with_timeout(socket, data, size, timeout_ms);
    }
    bool receive(void* data, size_t& size, uint64_t timeout_ms, bool strict)
    {
        return read_all_with_timeout(socket, data, size, timeout_ms, strict);
    }

    void close()
    {
        boost::system::error_code ec;
        socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket.close(ec);
    }

  private:
    boost::asio::io_context& ioContext;
    boost::asio::ip::tcp::resolver resolver;
    boost::asio::ip::tcp::socket socket;
};
