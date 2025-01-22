#pragma once
#include "beastdefs.hpp"
#include "logger.hpp"

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
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
