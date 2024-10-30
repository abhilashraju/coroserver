#pragma once
#include "beastdefs.hpp"
#include "make_awaitable.hpp"

inline AwaitableResult<net::ip::tcp::resolver::results_type>
    awaitable_resolve(typename net::ip::tcp::resolver& resolver,
                      const std::string& host, const std::string& port)
{
    auto h = make_awaitable_handler<net::ip::tcp::resolver::results_type>(
        [&](auto handler) {
            // resolver.async_resolve(
            //     host, port,
            //     [handler = std::move(handler)](
            //         boost::system::error_code ec,
            //         net::ip::tcp::resolver::results_type results) mutable {
            //         handler(ec, std::move(results));
            //     });
            boost::system::error_code ec;
            auto results = resolver.resolve(host, port, ec);
            handler(ec, results);
        });
    co_return co_await h();
}
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
                auto [ec, results] =
                    co_await awaitable_resolve(resolver_, host, port);
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
