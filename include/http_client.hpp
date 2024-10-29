#pragma once
#include "beastdefs.hpp"

template <typename Stream>
class HttpClient
{
  public:
    HttpClient(net::io_context& ioc, ssl::context& ctx) :
        ioc(ioc), stream_(ioc, ctx)
    {}
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    net::awaitable<boost::system::error_code>
        connect(const std::string& host, const std::string& port)
    {
        boost::system::error_code ec;
        if constexpr (std::is_same_v<Stream, tcp::socket>)
        {
            net::ip::tcp::resolver resolver_(ioc);
            try
            {
                const auto results = resolver_.resolve(host, port, ec);
                if (ec)
                    co_return ec;
                co_await net::async_connect(
                    stream_.next_layer(), results,
                    net::redirect_error(net::use_awaitable, ec));
                if (ec)
                    co_return ec;
            }
            catch (const std::exception& e)
            {
                co_return boost::system::errc::make_error_code(
                    boost::system::errc::host_unreachable);
            }
        }
        else if constexpr (std::is_same_v<Stream, unix_domain::socket>)
        {
            stream_.next_layer().connect(unix_domain::endpoint(host), ec);
            if (ec)
                co_return ec;
        }
        co_await stream_.async_handshake(
            ssl::stream_base::client,
            net::redirect_error(net::use_awaitable, ec));
        co_return ec;
    }

    net::awaitable<boost::system::error_code> send_request(const Request& req)
    {
        boost::system::error_code ec;
        co_await http::async_write(stream_, req,
                                   net::redirect_error(net::use_awaitable, ec));
        co_return ec;
    }

    net::awaitable<std::pair<boost::system::error_code, Response>>
        receive_response()
    {
        boost::system::error_code ec;
        boost::beast::flat_buffer buffer;
        Response res;
        co_await http::async_read(stream_, buffer, res,
                                  net::redirect_error(net::use_awaitable, ec));
        co_return std::make_pair(ec, res);
    }

  private:
    net::io_context& ioc;
    ssl::stream<Stream> stream_;
};
