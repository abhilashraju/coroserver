#pragma once
#include "beastdefs.hpp"
#include "tcp_client.hpp"
namespace NSNAME
{
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
        // Set the SNI hostname so the server (e.g. bmcweb) can select the
        // correct certificate during the TLS handshake.
        if constexpr (std::is_same_v<Stream, beast::tcp_stream>)
        {
            SSL_set_tlsext_host_name(stream_.native_handle(), host.c_str());
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
        Response res;
        setTimeout(5s);
        // beast::flat_buffer is required by http::async_read.
        co_await http::async_read(stream_, beastBuffer_, res,
                                  net::redirect_error(net::use_awaitable, ec));
        co_return std::make_pair(ec, res);
    }

    // Read only the response header (status line + header fields).
    // Returns the HTTP status code (e.g. 200, 404).
    // beast::http::parser is not movable so we extract the status here and
    // return only the integer — the body/stream is left on the wire for
    // subsequent readUntil() calls.
    net::awaitable<std::pair<boost::system::error_code, unsigned>>
        readResponseHeader()
    {
        boost::system::error_code ec;
        http::response_parser<http::string_body> parser;
        parser.eager(false); // stop after headers
        setTimeout(30s);
        co_await http::async_read_header(stream_, beastBuffer_,
                                         parser,
                                         net::redirect_error(net::use_awaitable, ec));
        unsigned status = ec ? 0u : parser.get().result_int();
        co_return std::make_pair(ec, status);
    }

    // Read raw bytes from the stream until `delim` is found.
    // Uses net::streambuf so extraction is a plain istream read — no iterator
    // arithmetic, no segment-boundary issues.
    // sseBuffer_ must be a member: async_read_until reads ahead past the
    // delimiter; those lookahead bytes must survive across calls so the next
    // frame is not lost.
    net::awaitable<std::pair<boost::system::error_code, std::string>>
        readUntil(const std::string& delim)
    {
        boost::system::error_code ec;
        std::size_t bytes = co_await net::async_read_until(
            stream_, sseBuffer_, delim,
            net::redirect_error(net::use_awaitable, ec));
        if (ec)
            co_return std::make_pair(ec, std::string{});
        std::string result(bytes, '\0');
        std::istream is(&sseBuffer_);
        is.read(result.data(), static_cast<std::streamsize>(bytes));
        co_return std::make_pair(ec, std::move(result));
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
    boost::beast::flat_buffer beastBuffer_; // used by http::async_read
    net::streambuf             sseBuffer_;  // used by readUntil (SSE frames)
};
} // namespace NSNAME
