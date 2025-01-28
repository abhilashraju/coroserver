#pragma once
#include "beastdefs.hpp"
#include "logger.hpp"
#include "make_awaitable.hpp"

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

#include <chrono>
using namespace std::chrono_literals;

struct TcpStreamType
{
    using stream_type = boost::asio::ssl::stream<tcp::socket>;
    tcp::acceptor acceptor_;
    boost::asio::io_context& context;
    boost::asio::ssl::context& ssl_context_;
    TcpStreamType(boost::asio::io_context& io_context, short port,
                  boost::asio::ssl::context& ssl_context) :
        acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
        context(io_context), ssl_context_(ssl_context)
    {}

    template <typename Handler>
    void accept(Handler&& handler)
    {
        auto socket = std::make_shared<stream_type>(context, ssl_context_);
        acceptor_.async_accept(socket->lowest_layer(),
                               [this, socket, handler = std::move(handler)](
                                   boost::system::error_code ec) {
                                   if (!ec)
                                   {
                                       handler(std::move(socket));
                                   }
                               });
    }
    auto get_remote_endpoint(stream_type& socket)
    {
        return socket.next_layer().remote_endpoint();
    }
};

struct UnixStreamType
{
    using stream_type =
        boost::asio::ssl::stream<boost::asio::local::stream_protocol::socket>;
    using unix_domain = boost::asio::local::stream_protocol;
    unix_domain::acceptor acceptor_;
    boost::asio::io_context& context;
    boost::asio::ssl::context& ssl_context_;
    UnixStreamType(boost::asio::io_context& io_context, const std::string& path,
                   boost::asio::ssl::context& ssl_context) :
        acceptor_(io_context,
                  boost::asio::local::stream_protocol::endpoint(path)),
        context(io_context), ssl_context_(ssl_context)
    {}

    template <typename Handler>
    void accept(Handler&& handler)
    {
        auto socket = std::make_shared<stream_type>(context, ssl_context_);
        acceptor_.async_accept(socket->lowest_layer(),
                               [this, socket, handler = std::move(handler)](
                                   boost::system::error_code ec) {
                                   if (!ec)
                                   {
                                       handler(std::move(socket));
                                   }
                               });
    }
    auto get_remote_endpoint(stream_type& socket)
    {
        return tcp::endpoint();
    }
};

template <typename StreamType>
struct TimedStreamer
{
    TimedStreamer(StreamType& socket, net::steady_timer& timer) :
        socket(socket), timer_(timer)
    {}
    AwaitableResult<std::size_t> read(net::mutable_buffer data)
    {
        setTimeout(30s);
        boost::system::error_code ec;
        auto bytes = co_await socket.async_read_some(
            data, boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        co_return std::make_pair(ec, bytes);
    }
    // AwaitableResult<std::size_t> readUntil(boost::beast::flat_buffer& buffer,
    //                                        std::string_view delim)
    // {
    //     setTimeout(30s);
    //     boost::system::error_code ec;
    //     auto bytes = co_await net::async_read_until(
    //         socket, buffer, delim,
    //         boost::asio::redirect_error(net::use_awaitable, ec));
    //     co_return std::make_pair(ec, bytes);
    // }
    AwaitableResult<std::string> readUntil(const std::string& delim,
                                           size_t max_size = 1024)
    {
        std::string ret;
        ret.reserve(max_size);
        std::string data(1024, '\0');
        while (ret.length() <= max_size - delim.length())
        {
            auto [ec, bytes] = co_await read(net::buffer(data));
            if (ec)
            {
                if (ec != boost::asio::error::eof)
                {
                    LOG_DEBUG("Error reading: {}", ec.message());
                }
                co_return std::make_tuple(ec, std::move(ret));
            }
            ret.append(data.data(), bytes);
            if (std::string_view{data.data(), bytes}.find(delim) !=
                std::string::npos)
            {
                co_return std::make_tuple(boost::system::error_code{},
                                          std::move(ret));
            }
            std::fill(data.begin(), data.end(), '\0');
        }
        co_return std::make_tuple(boost::system::error_code{}, std::move(ret));
    }

    AwaitableResult<std::size_t> write(net::const_buffer data)
    {
        setTimeout(30s);
        boost::system::error_code ec;
        auto bytes = co_await socket.async_write_some(
            data, boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        co_return std::make_pair(ec, bytes);
    }
    void setTimeout(std::chrono::seconds timeout)
    {
        timer_.expires_after(timeout);
        timer_.async_wait([this](const boost::system::error_code& ec) {
            if (!ec)
            {
                socket.next_layer().cancel();
            }
        });
    }
    void close()
    {
        boost::system::error_code ec;
        socket.next_layer().close(ec);
        if (ec)
        {
            LOG_ERROR("Error closing socket: {}", ec.message());
        }
    }
    bool isOpen() const
    {
        return socket.next_layer().is_open();
    }
    StreamType& socket;
    net::steady_timer& timer_;
};
