#pragma once
#include "beastdefs.hpp"
#include "flat_map.hpp"
#include "http_errors.hpp"
#include "http_target_parser.hpp"
#include "logger.hpp"
#include "request_mapper.hpp"

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

#include <concepts>

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
struct HttpRouter
{
    struct handler_base
    {
        virtual boost::asio::awaitable<Response>
            handle(Request& req, const http_function& vw) = 0;
        virtual ~handler_base() {}
    };
    using HANDLER_MAP = flat_map<request_mapper, std::unique_ptr<handler_base>>;
    template <typename HandlerFunc>
    struct handler : handler_base
    {
        HandlerFunc func;
        handler(HandlerFunc fun) : func(std::move(fun)) {}

        boost::asio::awaitable<Response>
            handle(Request& req, const http_function& params) override
        {
            return func(req, params);
        }
    };
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
        add_handler({{path.data(), path.length()}, http::verb::delete_}, path,
                    (FUNC&&)h);
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

    boost::asio::awaitable<Response>
        process_request(auto& reqVariant, tcp::endpoint&& ep)
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
    std::optional<std::reference_wrapper<net::io_context>> ioc;
};
struct TcpStreamType
{
    using stream_type = boost::asio::ssl::stream<tcp::socket>;
    tcp::acceptor acceptor_;
    boost::asio::io_context& context;
    boost::asio::ssl::context& ssl_context_;
    TcpStreamType(boost::asio::io_context& io_context, short port,
                  boost::asio::ssl::context& ssl_context) :
        acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
        context(io_context), ssl_context_(ssl_context)
    {}

    template <typename Handler>
    void accept(Handler&& handler)
    {
        auto socket = std::make_shared<stream_type>(context, ssl_context_);
        acceptor_.async_accept(socket->lowest_layer(),
                               [this, socket, handler = std::move(handler)](
                                   boost::system::error_code ec) {
                                   if (!ec)
                                   {
                                       handler(std::move(socket));
                                   }
                               });
    }
    auto get_remote_endpoint(stream_type& socket)
    {
        return socket.next_layer().remote_endpoint();
    }
};

struct UnixStreamType
{
    using stream_type =
        boost::asio::ssl::stream<boost::asio::local::stream_protocol::socket>;
    using unix_domain = boost::asio::local::stream_protocol;
    unix_domain::acceptor acceptor_;
    boost::asio::io_context& context;
    boost::asio::ssl::context& ssl_context_;
    UnixStreamType(boost::asio::io_context& io_context, const std::string& path,
                   boost::asio::ssl::context& ssl_context) :
        acceptor_(io_context,
                  boost::asio::local::stream_protocol::endpoint(path)),
        context(io_context), ssl_context_(ssl_context)
    {}

    template <typename Handler>
    void accept(Handler&& handler)
    {
        auto socket = std::make_shared<stream_type>(context, ssl_context_);
        acceptor_.async_accept(socket->lowest_layer(),
                               [this, socket, handler = std::move(handler)](
                                   boost::system::error_code ec) {
                                   if (!ec)
                                   {
                                       handler(std::move(socket));
                                   }
                               });
    }
    auto get_remote_endpoint(stream_type& socket)
    {
        return tcp::endpoint();
    }
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
    boost::asio::awaitable<void>
        handle_client(std::shared_ptr<boost::asio::ssl::stream<Socket>> socket)
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
        Response res;
        try
        {
            res = co_await router_.process_request(
                req, acceptor_.get_remote_endpoint(*socket));
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

    boost::asio::io_context& context;
    Accepter& acceptor_;
    HttpRouter& router_;
};
