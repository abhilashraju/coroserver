#pragma once
#include "make_awaitable.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <utility>
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;
using namespace std::chrono_literals;
using namespace std::chrono_literals;
inline AwaitableResult<net::ip::tcp::resolver::results_type>
    awaitable_resolve(typename net::ip::tcp::resolver& resolver,
                      const std::string& host, const std::string& port)
{
    auto h = make_awaitable_handler<net::ip::tcp::resolver::results_type>(
        [&](auto promise) {
            // resolver.async_resolve(
            //     host, port,
            //     [handler = std::move(handler)](
            //         boost::system::error_code ec,
            //         net::ip::tcp::resolver::results_type results) mutable {
            //         handler(ec, std::move(results));
            //     });
            boost::system::error_code ec;
            auto results = resolver.resolve(host, port, ec);
            promise.setValues(ec, results);
        });
    co_return co_await h();
}
class TcpClient
{
  public:
    TcpClient(net::io_context& io_context, ssl::context& ssl_context) :
        resolver_(io_context), stream_(io_context, ssl_context),
        timer_(io_context)
    {}

    net::awaitable<boost::system::error_code>
        connect(const std::string& host, const std::string& port)
    {
        auto [ec, results] = co_await awaitable_resolve(resolver_, host, port);
        if (ec)
            co_return ec;
        setTimeout(30s);
        co_await net::async_connect(
            stream_.next_layer(), results,
            net::redirect_error(net::use_awaitable, ec));
        if (ec)
            co_return ec;

        co_await stream_.async_handshake(
            ssl::stream_base::client,
            net::redirect_error(net::use_awaitable, ec));
        co_return ec;
    }

    AwaitableResult<std::size_t> write(net::const_buffer data)
    {
        setTimeout(30s);
        boost::system::error_code ec;
        auto bytes = co_await stream_.async_write_some(
            net::buffer(data), net::redirect_error(net::use_awaitable, ec));
        co_return std::make_tuple(ec, bytes);
    }

    AwaitableResult<std::size_t> read(net::mutable_buffer buffer)
    {
        setTimeout(30s);
        boost::system::error_code ec;
        std::size_t bytes = co_await stream_.async_read_some(
            buffer, net::redirect_error(net::use_awaitable, ec));
        co_return std::make_tuple(ec, bytes);
    }

  private:
    void setTimeout(std::chrono::seconds timeout)
    {
        timer_.expires_after(timeout);
        timer_.async_wait([this](const boost::system::error_code& ec) {
            if (!ec)
            {
                stream_.next_layer().cancel();
            }
        });
    }

    tcp::resolver resolver_;
    ssl::stream<tcp::socket> stream_;
    net::steady_timer timer_;
};
