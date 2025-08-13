
#include "logger.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <cstring>
#include <iostream>
namespace net = boost::asio;
using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
namespace this_coro = boost::asio::this_coro;

awaitable<void> lldp_listener(net::io_context& io_context)
{
    int sock = ::socket(AF_PACKET, SOCK_RAW, htons(0x88cc));
    if (sock < 0)
    {
        LOG_ERROR("Failed to create socket: {}", strerror(errno));
        co_return;
    }

    boost::asio::posix::stream_descriptor sd(io_context, sock);

    unsigned char buffer[2048];
    while (true)
    {
        std::size_t len = co_await sd.async_read_some(
            boost::asio::buffer(buffer), use_awaitable);
        if (len > 0)
        {
            LOG_DEBUG("Received LLDP packet of length: {}", len);
            LOG_DEBUG("LLDP packet data: {}",
                      std::string(reinterpret_cast<char*>(buffer), len));
            // Optionally, parse buffer+14 for LLDP TLVs
        }
    }
    // sd will close the socket automatically
}

int main()
{
    boost::asio::io_context io;
    co_spawn(io, std::bind_front(lldp_listener, std::ref(io)), detached);
    io.run();
    return 0;
}
