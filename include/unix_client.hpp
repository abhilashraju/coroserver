#pragma once
#include "make_awaitable.hpp"
#include "socket_streams.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast.hpp>
#include <boost/system/error_code.hpp>

#include <iostream>
#include <string>
#include <utility>

namespace NSNAME
{
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using unix_domain = boost::asio::local::stream_protocol;

class UnixClient
{
  public:
    UnixClient(net::any_io_executor io_context, ssl::context& ssl_context) :
        stream_(std::make_shared<ssl::stream<unix_domain::socket>>(
            io_context, ssl_context)),
        timer_(std::make_shared<net::steady_timer>(io_context))
    {}
    ~UnixClient()
    {
        timer_->cancel();
    }

    net::awaitable<boost::system::error_code> connect(const std::string& path)
    {
        boost::system::error_code ec;
        unix_domain::endpoint endpoint(path);

        TimedStreamer streamer(stream_, timer_);
        streamer.setTimeout(30s);
        co_await stream_->next_layer().async_connect(
            endpoint, net::redirect_error(net::use_awaitable, ec));
        if (ec)
        {
            LOG_ERROR("Error connecting to {}. Error: {}", path, ec.message());
            co_return ec;
        }
        streamer.setTimeout(30s);
        co_await stream_->async_handshake(
            ssl::stream_base::client,
            net::redirect_error(net::use_awaitable, ec));
        if (ec)
        {
            LOG_ERROR("Error during SSL handshake with {}. Error: {}", path,
                      ec.message());
        }
        co_return ec;
    }

    AwaitableResult<std::size_t> write(net::const_buffer data)
    {
        co_return co_await streamer().write(data);
    }

    AwaitableResult<std::size_t> read(net::mutable_buffer buffer)
    {
        co_return co_await streamer().read(buffer);
    }
    ssl::stream<unix_domain::socket>& stream()
    {
        return *stream_;
    }
    TimedStreamer<ssl::stream<unix_domain::socket>> streamer()
    {
        return TimedStreamer(stream_, timer_);
    }
    void close()
    {
        boost::system::error_code ec;
        stream_->next_layer().close(ec);
        if (ec)
        {
            LOG_ERROR("Error closing socket: {}", ec.message());
        }
    }
    bool isOpen() const
    {
        return stream_->lowest_layer().is_open();
    }

  private:
    std::shared_ptr<ssl::stream<unix_domain::socket>> stream_;
    std::shared_ptr<net::steady_timer> timer_;
};
} // namespace NSNAME
