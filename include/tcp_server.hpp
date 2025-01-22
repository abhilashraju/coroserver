#pragma once
#include "make_awaitable.hpp"
#include "socket_streams.hpp"

#include <concepts>
#include <string>
#include <string_view>

template <typename Accepter, typename Router>
class TcpServer
{
  public:
    struct Reader
    {
        Reader(typename Accepter::stream_type& socket) : socket(socket) {}
        AwaitableResult<std::size_t> read(net::mutable_buffer data)
        {
            boost::system::error_code ec;
            auto bytes = co_await socket.async_read_some(
                data,
                boost::asio::redirect_error(boost::asio::use_awaitable, ec));
            co_return std::make_pair(ec, bytes);
        }
        AwaitableResult<std::size_t>
            readUntil(boost::beast::flat_buffer& buffer, std::string_view delim)
        {
            boost::system::error_code ec;
            auto bytes = co_await net::async_read_until(
                socket, buffer, delim,
                boost::asio::redirect_error(net::use_awaitable, ec));
            co_return std::make_pair(ec, bytes);
        }
        typename Accepter::stream_type& socket;
    };
    struct Writer
    {
        Writer(typename Accepter::stream_type& socket) : socket(socket) {}
        AwaitableResult<std::size_t> write(net::const_buffer data)
        {
            boost::system::error_code ec;
            auto bytes = co_await socket.async_write_some(
                data,
                boost::asio::redirect_error(boost::asio::use_awaitable, ec));
            co_return std::make_pair(ec, bytes);
        }
        typename Accepter::stream_type& socket;
    };
    TcpServer(boost::asio::io_context& io_context, Accepter& accepter,
              Router& router) :
        context(io_context), acceptor_(accepter), router_(router)
    {
        start_accept();
    }

  private:
    void start_accept()
    {
        // auto socket =
        // std::make_shared<boost::asio::ssl::stream<tcp::socket>>(
        //     context, ssl_context_);
        // acceptor_.async_accept(
        //     socket->lowest_layer(),
        //     [this, socket](boost::system::error_code ec) {
        //         if (!ec)
        //         {
        //             boost::asio::co_spawn(context, handle_client(socket),
        //                                   boost::asio::detached);
        //         }
        //         start_accept();
        //     });
        acceptor_.accept([this](auto&& socket) {
            boost::asio::co_spawn(context, handle_client(socket),
                                  boost::asio::detached);
            start_accept();
        });
    }
    template <typename Socket>
    boost::asio::awaitable<void>
        handle_client(std::shared_ptr<boost::asio::ssl::stream<Socket>> socket)
    {
        // Perform SSL handshake
        co_await socket->async_handshake(boost::asio::ssl::stream_base::server,
                                         boost::asio::use_awaitable);

        co_await router_(Reader{*socket}, Writer{*socket});
    }

    boost::asio::io_context& context;
    Accepter& acceptor_;
    Router& router_;
};
