#pragma once
#include "beastdefs.hpp"
#include "tcp_client.hpp"

#include <memory>
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
        co_return co_await receive_response_as<http::string_body>();
    }

    // Generic body reader — caller pre-constructs the response (e.g. with an
    // already-opened file_body value) or leaves it default-initialised for
    // string_body / dynamic_body.
    // timeout defaults to 5s for normal API responses; pass a larger value
    // (e.g. 300s) when downloading large firmware images.
    // bodyLimit defaults to unlimited (max uint64) to support large firmware
    // tarballs; pass a smaller value to cap memory usage for string/buffer
    // responses.
    template <typename BodyType>
    net::awaitable<
        std::pair<boost::system::error_code, http::response<BodyType>>>
        receive_response_as(
            http::response<BodyType> res = {},
            std::chrono::seconds timeout = 5s,
            std::uint64_t bodyLimit = std::numeric_limits<std::uint64_t>::max())
    {
        boost::system::error_code ec;
        setTimeout(timeout);
        // Use response_parser so we can raise (or cap) the body-size limit.
        // Beast's default is 8 MB which is too small for firmware tarballs.
        http::response_parser<BodyType> parser{std::move(res)};
        parser.body_limit(bodyLimit);
        co_await http::async_read(stream_, beastBuffer_, parser,
                                  net::redirect_error(net::use_awaitable, ec));
        co_return std::make_pair(ec, parser.release());
    }

    // Read only the response header (status line + header fields).
    // Stores the empty_body parser as a member so readBodyAs<BodyType>() can
    // move-construct from it, transferring all parsed header state (status,
    // Content-Length, Transfer-Encoding, etc.) into the body-typed parser.
    // Returns the HTTP status code (e.g. 200, 404).
    net::awaitable<std::pair<boost::system::error_code, unsigned>>
        readResponseHeader()
    {
        boost::system::error_code ec;
        headerParser_ =
            std::make_unique<http::response_parser<http::empty_body>>();
        // Lift the body-size limit on the header parser too — Beast checks
        // Content-Length against the limit even during header-only reads, so a
        // firmware tarball's Content-Length would fail here before any body
        // bytes are read.
        headerParser_->body_limit(std::numeric_limits<std::uint64_t>::max());
        setTimeout(30s);
        co_await http::async_read_header(
            stream_, beastBuffer_, *headerParser_,
            net::redirect_error(net::use_awaitable, ec));
        unsigned status = ec ? 0u : headerParser_->get().result_int();
        if (ec)
            headerParser_.reset();
        co_return std::make_pair(ec, status);
    }

    // Read the response body using the header state saved by
    // readResponseHeader().  Move-constructs a body-typed parser from the
    // stored empty_body parser so Beast continues from where it left off —
    // same buffer, same parser state, no re-parsing of headers.
    // bodyLimit defaults to unlimited for large downloads (firmware tarballs).
    template <typename BodyType>
    net::awaitable<
        std::pair<boost::system::error_code, http::response<BodyType>>>
        readBodyAs(
            http::response<BodyType> seed = {},
            std::chrono::seconds timeout = 5s,
            std::uint64_t bodyLimit = std::numeric_limits<std::uint64_t>::max())
    {
        if (!headerParser_)
        {
            co_return std::make_pair(boost::system::errc::make_error_code(
                                         boost::system::errc::invalid_argument),
                                     http::response<BodyType>{});
        }
        // Move the header parser state into a body-typed parser.
        // body_limit must be set before the move so the new parser inherits it.
        headerParser_->body_limit(bodyLimit);
        http::response_parser<BodyType> bodyParser{std::move(*headerParser_)};
        headerParser_.reset();
        // Inject only the body value (e.g. an already-opened file handle) —
        // do NOT replace the whole response or the parsed headers are lost.
        bodyParser.get().body() = std::move(seed.body());
        boost::system::error_code ec;
        setTimeout(timeout);
        co_await http::async_read(stream_, beastBuffer_, bodyParser,
                                  net::redirect_error(net::use_awaitable, ec));
        co_return std::make_pair(ec, bodyParser.release());
    }

    // Reset all per-request parser state: the stored header parser and the
    // flat read buffer.  Called automatically by
    // PooledConnection::markInvalid() and should also be called before reusing
    // a connection from the idle pool to guarantee no leftover bytes from a
    // previous response corrupt the next request's header parse.
    void resetParserState()
    {
        headerParser_.reset();
        beastBuffer_.clear();
    }

    // Read raw bytes from the stream until `delim` is found.
    // Uses net::streambuf so extraction is a plain istream read — no iterator
    // arithmetic, no segment-boundary issues.
    // sseBuffer_ must be a member: async_read_until reads ahead past the
    // delimiter; those lookahead bytes must survive across calls so the next
    // frame is not lost.
    net::awaitable<std::pair<boost::system::error_code, std::string>> readUntil(
        const std::string& delim)
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
    net::streambuf sseBuffer_;              // used by readUntil (SSE frames)
    // Stored between readResponseHeader() and readBodyAs() calls.
    std::unique_ptr<http::response_parser<http::empty_body>> headerParser_;
};
} // namespace NSNAME
