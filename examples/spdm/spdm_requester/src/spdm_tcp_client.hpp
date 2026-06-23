#pragma once
#include "logger.hpp"
#include "socket_functions.hpp"

#include <boost/asio.hpp>

#include <functional>

// Default timeout for socket operations (10 seconds in milliseconds)
constexpr uint64_t DEFAULT_SOCKET_TIMEOUT_MS = 10000;

class SpdmTcpClient
{
  public:
    enum class CloseReason
    {
        SendFailed,
        ReceiveFailed,
        ConnectionLost
    };

    using OnCloseCallback = std::function<void(CloseReason)>;

    SpdmTcpClient(boost::asio::io_context& io_context) :
        ioContext(io_context), resolver(io_context), socket(io_context), host(),
        port(0), onCloseCallback(nullptr)
    {}

    void onClose(OnCloseCallback callback)
    {
        onCloseCallback = std::move(callback);
    }

    boost::system::error_code connect(
        const std::string& hostAddr, uint16_t portNum,
        uint64_t timeout_ms = DEFAULT_SOCKET_TIMEOUT_MS)
    {
        host = hostAddr;
        port = portNum;
        return connectInternal(timeout_ms);
    }

    bool send(const void* data, size_t size, uint64_t timeout_ms)
    {
        bool result = write_with_timeout(socket, data, size, timeout_ms);
        if (!result && onCloseCallback)
        {
            onCloseCallback(CloseReason::SendFailed);
        }
        return result;
    }

    bool receive(void* data, size_t& size, uint64_t timeout_ms, bool strict)
    {
        bool result =
            read_all_with_timeout(socket, data, size, timeout_ms, strict);
        if (!result && onCloseCallback)
        {
            onCloseCallback(CloseReason::ReceiveFailed);
        }
        return result;
    }

    void close()
    {
        boost::system::error_code ec;
        socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket.close(ec);
    }

  private:
    boost::system::error_code connectInternal(uint64_t timeout_ms)
    {
        // Close existing connection if any
        boost::system::error_code ec;
        if (socket.is_open())
        {
            socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            socket.close(ec);

            // Create a new socket object after closing the old one
            socket = boost::asio::ip::tcp::socket(ioContext);
        }

        // Resolve hostname
        auto endpoints = resolver.resolve(host, std::to_string(port), ec);
        if (ec)
        {
            LOG_ERROR("Error resolving {}:{}. Error: {}", host, port,
                      ec.message());
            return ec;
        }

        // Connect to endpoint
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

    boost::asio::io_context& ioContext;
    boost::asio::ip::tcp::resolver resolver;
    boost::asio::ip::tcp::socket socket;
    std::string host;
    uint16_t port;
    OnCloseCallback onCloseCallback;
};

// Made with Bob
