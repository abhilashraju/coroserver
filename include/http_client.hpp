#pragma once
#include "beastdefs.hpp"
#include "tcp_client.hpp"

template <typename Stream>
class HttpClient
{
  public:
    HttpClient(net::io_context& ioc, ssl::context& ctx) :
        ioc(ioc), stream_(ioc, ctx)
    {}
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    net::awaitable<boost::system::error_code> connect(const std::string& host,
                                                      const std::string& port)
    {
        boost::system::error_code ec;
        if constexpr (std::is_same_v<Stream, beast::tcp_stream>)
        {
            net::ip::tcp::resolver resolver_(ioc);
            try
            {
                auto [ec, results] =
                    co_await awaitable_resolve(resolver_, host, port);
                if (ec)
                    co_return ec;
                setTimeout(5s);
                co_await getLowestLayer().async_connect(
                    results, net::redirect_error(net::use_awaitable, ec));
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
            getLowestLayer().connect(unix_domain::endpoint(host), ec);
            if (ec)
                co_return ec;
        }
        setTimeout(5s);
        co_await stream_.async_handshake(
            ssl::stream_base::client,
            net::redirect_error(net::use_awaitable, ec));
        co_return ec;
    }

    net::awaitable<boost::system::error_code> send_request(const Request& req)
    {
        boost::system::error_code ec;
        setTimeout(5s);
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
        setTimeout(5s);
        co_await http::async_read(stream_, buffer, res,
                                  net::redirect_error(net::use_awaitable, ec));
        co_return std::make_pair(ec, res);
    }
    auto getExecutor() -> net::io_context::executor_type
    {
        return ioc.get_executor();
    }
    void cancel()
    {
        getLowestLayer().cancel();
    }
    auto& getLowestLayer()
    {
        return beast::get_lowest_layer(stream_);
    }

  private:
    void setTimeout(std::chrono::seconds seconds)
    {
        if constexpr (std::is_same_v<Stream, beast::tcp_stream>)
        {
            beast::get_lowest_layer(stream_).expires_after(seconds);
        }
    }
    net::io_context& ioc;
    ssl::stream<Stream> stream_;
};
