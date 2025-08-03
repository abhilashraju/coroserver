#include "logger.hpp"

#include <boost/asio.hpp>

#include <chrono>
#include <iostream>
using boost::asio::ip::tcp;

bool write_with_timeout(tcp::socket& socket, const void* data,
                        std::size_t& size, uint64_t timeout_ms)
{
    boost::system::error_code ec;
    boost::asio::streambuf buf;
    std::ostream os(&buf);
    os.write(static_cast<const char*>(data), size);
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
                       uint64_t timeout_ms)
{
    // socket.non_blocking(true);
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
