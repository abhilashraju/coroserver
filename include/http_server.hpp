#pragma once

#include "flat_map.hpp"
#include "http_errors.hpp"
#include "http_target_parser.hpp"
#include "request_mapper.hpp"
#include "socket_streams.hpp"

#include <concepts>
#include <functional>
#include <unordered_map>
namespace NSNAME
{

// A write-only handle passed to SSE handlers so they can push events
// to the client without holding a reference to the raw socket.
struct SseWriter
{
    // Write one SSE event.  Returns false when the connection is gone.
    std::function<boost::asio::awaitable<bool>(std::string /*data*/)> write;
};

template <typename T>
concept AwaitableResponseHandler =
    requires(T t, Request& req, const http_function& params) {
        { t(req, params) } -> std::same_as<net::awaitable<Response>>;
    };
template <typename T>
concept AwaitableYieldResponseHandler =
    requires(T t, Request& req, const http_function& params,
             net::yield_context yc) {
        { t(req, params, yc) } -> std::same_as<net::awaitable<Response>>;
    };
template <typename T>
concept ValueResponseHandler =
    requires(T t, Request& req, const http_function& params) {
        { t(req, params) } -> std::same_as<Response>;
    };
template <typename HandlerFunc>
auto make_awitable_handler(HandlerFunc&& h)
{
    return [h = std::forward<HandlerFunc>(h)](auto& req, auto& params)
               -> net::awaitable<Response> { co_return h(req, params); };
}

// Concept for SSE streaming handlers: (Request&, http_function&, SseWriter) -> awaitable<void>
template <typename T>
concept SseHandler =
    requires(T t, Request& req, const http_function& params, SseWriter w) {
        { t(req, params, w) } -> std::same_as<net::awaitable<void>>;
    };

struct HttpRouter
{
    struct handler_base
    {
        virtual boost::asio::awaitable<Response> handle(
            Request& req, const http_function& vw) = 0;
        virtual ~handler_base() {}
    };
    using HANDLER_MAP = flat_map<request_mapper, std::unique_ptr<handler_base>>;
    template <typename HandlerFunc>
    struct handler : handler_base
    {
        HandlerFunc func;
        handler(HandlerFunc fun) : func(std::move(fun)) {}

        boost::asio::awaitable<Response> handle(
            Request& req, const http_function& params) override
        {
            return func(req, params);
        }
    };

    // Fallback handler storage
    std::unique_ptr<handler_base> fallback_handler;
    void setIoContext(std::reference_wrapper<net::io_context> ctx)
    {
        ioc = ctx;
    }
    template <AwaitableResponseHandler FUNC>
    void add_handler(const request_mapper& mapper, FUNC&& h)
    {
        auto& handlers = handler_for_verb(mapper.method);
        handlers[mapper] = std::make_unique<handler<FUNC>>(std::move(h));
    }
    template <AwaitableResponseHandler FUNC>
    void add_get_handler(std::string_view path, FUNC&& h)
    {
        add_handler({{path.data(), path.length()}, http::verb::get}, (FUNC&&)h);
    }
    template <ValueResponseHandler FUNC>
    void add_get_handler(std::string_view path, FUNC&& h)
    {
        add_handler({{path.data(), path.length()}, http::verb::get},
                    make_awitable_handler((FUNC&&)h));
    }

    template <AwaitableResponseHandler FUNC>
    void add_post_handler(std::string_view path, FUNC&& h)
    {
        add_handler({{path.data(), path.length()}, http::verb::post},
                    (FUNC&&)h);
    }

    template <AwaitableResponseHandler FUNC>
    void add_put_handler(std::string_view path, FUNC&& h)
    {
        add_handler({{path.data(), path.length()}, http::verb::put}, (FUNC&&)h);
    }

    template <AwaitableResponseHandler FUNC>
    void add_delete_handler(std::string_view path, FUNC&& h)
    {
        add_handler({{path.data(), path.length()}, http::verb::delete_},
                    (FUNC&&)h);
    }

    // Register a Server-Sent Events streaming handler.
    // The handler receives an SseWriter and is expected to co_await it
    // indefinitely (or until the client disconnects).
    // Internally stored as a type-erased std::function.
    template <SseHandler FUNC>
    void add_sse_handler(std::string_view path, FUNC&& h)
    {
        sse_handlers[std::string(path)] =
            [h = std::forward<FUNC>(h)](Request& req,
                                        const http_function& params,
                                        SseWriter writer) -> net::awaitable<void> {
            co_await h(req, params, writer);
        };
    }

    using SseHandlerFn = std::function<net::awaitable<void>(
        Request&, const http_function&, SseWriter)>;

    SseHandlerFn* findSseHandler(const std::string& path)
    {
        auto it = sse_handlers.find(path);
        if (it == sse_handlers.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    // Set a fallback handler that will be called when no route matches
    template <AwaitableResponseHandler FUNC>
    void set_fallback_handler(FUNC&& h)
    {
        fallback_handler = std::make_unique<handler<FUNC>>(std::move(h));
    }

    HANDLER_MAP& handler_for_verb(http::verb v)
    {
        switch (v)
        {
            case http::verb::get:
                return get_handlers;
            case http::verb::put:
            case http::verb::post:
                return post_handlers;
            case http::verb::delete_:
                return delete_handlers;
            default:
                break;
        }
        return empty_handlers;
    }

    boost::asio::awaitable<Response> process_request(auto& reqVariant,
                                                     tcp::endpoint&& ep)
    {
        auto httpfunc = parse_function(reqVariant.target());
        httpfunc.setEndpoint(std::move(ep));
        auto& handlers = handler_for_verb(reqVariant.method());
        if (auto iter = handlers.find({httpfunc.name(), reqVariant.method()});
            iter != std::end(handlers))
        {
            extract_params_from_path(httpfunc, iter->first.path,
                                     httpfunc.name());
            co_return co_await iter->second->handle(reqVariant, httpfunc);
        }

        // If no route matched, try fallback handler
        if (fallback_handler)
        {
            co_return co_await fallback_handler->handle(reqVariant, httpfunc);
        }

        co_return make_error_response(reqVariant, httpfunc.name());
    }

    Response make_error_response(Request& req, const std::string& message)
    {
        Response res{http::status::not_found, 11};
        res.set(http::field::content_type, "text/plain");
        res.keep_alive(false);
        res.body() = std::format("Not Found {}", message);
        res.prepare_payload();
        return res;
    }

    HttpRouter* getForwarder(const std::string& path)
    {
        return this;
    }

    HANDLER_MAP get_handlers;
    HANDLER_MAP post_handlers;
    HANDLER_MAP delete_handlers;
    HANDLER_MAP empty_handlers;
    std::unordered_map<std::string, SseHandlerFn> sse_handlers;
    std::optional<std::reference_wrapper<net::io_context>> ioc;
};

template <typename Accepter>
class HttpServer
{
  public:
    HttpServer(boost::asio::io_context& io_context, Accepter& accepter,
               HttpRouter& router) :
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
    boost::asio::awaitable<void> handle_client(
        std::shared_ptr<boost::asio::ssl::stream<Socket>> socket)
    {
        // Perform SSL handshake
        co_await socket->async_handshake(boost::asio::ssl::stream_base::server,
                                         boost::asio::use_awaitable);

        beast::flat_buffer buffer;
        Request req;

        // Read the HTTP request
        boost::system::error_code ec;
        co_await http::async_read(
            *socket, buffer, req,
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        if (ec)
        {
            std::cerr << "Error reading request: " << ec.message() << std::endl;
            co_return;
        }

        std::cout << "Received request: " << req.body().data() << std::endl;

        // Check if this is an SSE subscription request
        auto httpfunc = parse_function(req.target());
        if (req.method() == http::verb::get)
        {
            if (auto* sseFn = router_.findSseHandler(httpfunc.name()))
            {
                co_await handle_sse_client(socket, req, httpfunc, *sseFn);
                co_return;
            }
        }

        Response res;
        try
        {
            res = co_await router_.process_request(
                req, acceptor_.getRemoteEndpoint(*socket));
        }
        catch (const std::exception& e)
        {
            res = make_internal_server_error("Internal Server Error",
                                             req.version());
        }
        // Write the response
        co_await http::async_write(
            *socket, res,
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        if (ec)
        {
            std::cerr << "Error writing response: " << ec.message()
                      << std::endl;
            co_return;
        }
        // Close the socket
        co_await socket->async_shutdown(
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        if (ec)
        {
            std::cerr << "Error shutting down SSL: " << ec.message()
                      << std::endl;
        }
    }

    // Handle a long-lived SSE connection: send headers then delegate to the
    // registered SSE handler which writes events via SseWriter.
    template <typename Socket>
    boost::asio::awaitable<void> handle_sse_client(
        std::shared_ptr<boost::asio::ssl::stream<Socket>> socket, Request& req,
        const http_function& httpfunc, HttpRouter::SseHandlerFn& sseFn)
    {
        // Send SSE response headers (no body yet — keep connection open)
        http::response<http::empty_body> headRes{http::status::ok,
                                                 req.version()};
        headRes.set(http::field::content_type, "text/event-stream");
        headRes.set(http::field::cache_control, "no-cache");
        headRes.set(http::field::connection, "keep-alive");
        headRes.set("Access-Control-Allow-Origin", "*");
        headRes.chunked(true);

        boost::system::error_code ec;
        http::response_serializer<http::empty_body> sr{headRes};
        co_await http::async_write_header(
            *socket, sr,
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        if (ec)
        {
            co_return;
        }

        // Build an SseWriter that writes raw SSE chunks directly to the socket
        SseWriter writer;
        writer.write = [&socket](std::string data) -> net::awaitable<bool> {
            // SSE frame: "data: <payload>\n\n"
            std::string frame = "data: " + data + "\n\n";
            // Write as a chunked body piece
            auto buf = net::buffer(frame);
            boost::system::error_code wec;
            co_await net::async_write(
                *socket, http::make_chunk(buf),
                boost::asio::redirect_error(boost::asio::use_awaitable, wec));
            co_return !wec;
        };

        try
        {
            co_await sseFn(req, httpfunc, writer);
        }
        catch (...)
        {
            // Client disconnected or handler threw — fall through to close
        }

        // Send chunked terminator
        co_await net::async_write(
            *socket, http::make_chunk_last(),
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));

        co_await socket->async_shutdown(
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));
    }

    boost::asio::io_context& context;
    Accepter& acceptor_;
    HttpRouter& router_;
};
} // namespace NSNAME
