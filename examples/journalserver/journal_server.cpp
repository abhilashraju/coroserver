
#include "command_line_parser.hpp"
#include "http_server.hpp"
#include "logger.hpp"
#include "pam_functions.hpp"
#include "webclient.hpp"

#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/process.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
// Forward declaration
class HttpSubscription;

// Subscription list to store HttpSubscription objects
std::unordered_set<std::shared_ptr<HttpSubscription>> subscriptionList;
bool journalRunning = false;

class HttpSubscription
{
  public:
    explicit HttpSubscription(net::io_context& ioc, ssl::context& ctx,
                              const std::string& url) :
        url(url), timer(ioc), client(ioc, ctx)
    {
        client.withUrl(boost::urls::parse_uri(url).value());
        client.withMethod(http::verb::post);
        client.withHeaders({{"Content-Type", "application/json"},
                            {"User-Agent", "JournalServer/1.0"},
                            {"Connection", "keep-alive"}});
    }

    // Notify method to send data to the subscriber
    void notify(const std::string& message)
    {
        bool emptyQueue = messageQueue.empty();
        messageQueue.push_back(message);
        if (emptyQueue)
        {
            net::co_spawn(
                timer.get_executor(),
                [this]() -> net::awaitable<void> { co_await sendMessage(); },
                net::detached);
        }
    }
    net::awaitable<void> sendMessage()
    {
        while (!messageQueue.empty())
        {
            auto message = messageQueue.front();

            auto [ec, response] = co_await client.withBody(message)
                                      .executeAndReturnAs<Response>();
            if (ec)
            {
                timer.expires_after(std::chrono::seconds(10));
                co_await timer.async_wait(net::use_awaitable);
                continue;
            }
            messageQueue.pop_front();
            LOG_INFO("Message sent to {}: {}", url, response.body());
        }
    }
    struct Hash
    {
        std::size_t operator()(
            const std::shared_ptr<HttpSubscription>& sub) const
        {
            return std::hash<std::string>()(sub->url);
        }
    };

  private:
    std::string url;
    std::deque<std::string> messageQueue;
    boost::asio::steady_timer timer;
    WebClient<beast::tcp_stream> client;
};

// Function to add a subscriber
bool subscribe(boost::asio::io_context& io_ctx,
               boost::asio::ssl::context& sslCtx, const std::string& url)
{
    boost::urls::url_view urlView(url);

    auto sub = std::make_shared<HttpSubscription>(io_ctx, sslCtx, url);
    return subscriptionList.insert(sub).second;
}

// Function to notify all subscribers (dummy HTTP POST, replace with real
// implementation)
void notifySubscribers(const std::string& message)
{
    for (const auto& sub : subscriptionList)
    {
        sub->notify(message);
    }
}
net::awaitable<void> monitorJournal(boost::asio::io_context& io_context)
{
    namespace bp = boost::process;
    bp::async_pipe pipe_out(io_context);

    // Start journalctl process
    bp::child c("/usr/bin/journalctl", "-f", bp::std_out > pipe_out);
    while (c.running())
    {
        std::vector<char> buf(1028);
        boost::system::error_code ec{};

        auto size = co_await net::async_read(
            pipe_out, net::buffer(buf),
            net::redirect_error(net::use_awaitable, ec));
        if (ec && ec != net::error::eof)
        {
            LOG_INFO("Error: {}", ec.message());
            break;
        }

        std::string line(buf.data(), size);

        if (!line.empty())
        {
            notifySubscribers(line);
        }
    }
}
// Coroutine to spawn journalctl and capture output asynchronously
void startJournalMonitor(boost::asio::io_context& io_context)
{
    if (!journalRunning)
    {
        journalRunning = true;
        boost::asio::co_spawn(
            io_context,
            [&]() -> net::awaitable<void> {
                co_await monitorJournal(io_context);
            },
            net::detached);
    }
}
int main(int argc, const char* argv[])
{
    using namespace reactor;
    try
    {
        getLogger().setLogLevel(LogLevel::DEBUG);
        LOG_INFO("Starting Journal Server");
        auto [cert] = getArgs(parseCommandline(argc, argv), "--cert,-c");

        boost::asio::io_context io_context;

        boost::asio::ssl::context ssl_context(
            boost::asio::ssl::context::sslv23);

        // Load server certificate and private key
        ssl_context.set_options(boost::asio::ssl::context::default_workarounds |
                                boost::asio::ssl::context::no_sslv2 |
                                boost::asio::ssl::context::single_dh_use);
        std::cerr << "Cert Loading: \n";
        std::string certDir = "/etc/ssl/private";
        if (cert)
        {
            certDir = std::string(*cert);
        }
        ssl_context.use_certificate_chain_file(certDir + "/server-cert.pem");
        ssl_context.use_private_key_file(certDir + "/server-key.pem",
                                         boost::asio::ssl::context::pem);
        std::cerr << "Cert Loaded: \n";
        HttpRouter router;
        router.setIoContext(io_context);
        TcpStreamType acceptor(io_context.get_executor(), 8080, ssl_context);
        HttpServer server(io_context, acceptor, router);
        router.add_post_handler(
            "/subscribe",
            [&](auto& req, auto& params) -> net::awaitable<Response> {
                auto url = req.body();
                if (url.empty())
                {
                    co_return make_bad_request_error("URL is required",
                                                     req.version());
                }
                if (!subscribe(io_context, ssl_context, url))
                {
                    co_return make_bad_request_error("Invalid URL",
                                                     req.version());
                }
                nlohmann::json jsonResponse;
                jsonResponse["status"] = "subscribed";
                startJournalMonitor(io_context);
                co_return make_success_response(jsonResponse, http::status::ok,
                                                req.version());
            });
        io_context.run();
    }
    catch (std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return 1;
    }

    return 0;
}
