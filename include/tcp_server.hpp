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
    using Streamer = TimedStreamer<typename Accepter::stream_type>;
    TcpServer(boost::asio::io_context& io_context, Accepter& accepter,
              Router& router) :
        context(io_context), acceptor_(accepter), router_(router),
        timer_(io_context)
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

        co_await router_(Streamer(*socket, timer_));
    }

    boost::asio::io_context& context;
    Accepter& acceptor_;
    Router& router_;
    net::steady_timer timer_;
};
