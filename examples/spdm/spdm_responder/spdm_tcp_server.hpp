#pragma once
#include "logger.hpp"
#include "socket_functions.hpp"

#include <boost/asio.hpp>

#include <cstdint>
#include <memory>
#include <vector>
// Dummy TCP socket abstraction

class SpdmTcpServer
{
  public:
    SpdmTcpServer(boost::asio::io_context& ioCtx, uint16_t port) :
        ioContext(ioCtx),
        acceptor(ioCtx, boost::asio::ip::tcp::endpoint(
                            boost::asio::ip::tcp::v4(), port)),
        socket(ioCtx)
    {}

    bool accept()
    {
        socket = boost::asio::ip::tcp::socket(ioContext);
        boost::system::error_code ec;
        acceptor.accept(socket, ec);
        if (ec)
        {
            LOG_ERROR("Accept failed: {}", ec.message());
            return false;
        }
        return true;
    }

    bool send(const void* data, size_t& size, uint64_t timeout_ms)
    {
        return write_with_timeout(socket, data, size, timeout_ms);
    }

    bool receive(void* data, size_t& size, uint64_t timeout_ms)
    {
        return read_with_timeout(socket, data, size, timeout_ms);
    }

    void close()
    {
        boost::system::error_code ec;
        socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        socket.close(ec);
    }

  private:
    boost::asio::io_context& ioContext;
    boost::asio::ip::tcp::acceptor acceptor;
    boost::asio::ip::tcp::socket socket;
};
