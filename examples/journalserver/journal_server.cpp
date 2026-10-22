
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
using namespace NSNAME;
// Maximum number of undelivered lines kept per subscriber. When a client is
// slower than the journal, the oldest lines are dropped.
constexpr std::size_t maxQueuedLines = 1000;

// One connected SSE client. Lines are queued here by the journal reader and
// drained by the client's own SSE handler coroutine on the same connection.
class Subscriber
{
  public:
    explicit Subscriber(const net::any_io_executor& executor) : signal(executor)
    {}

    void push(const std::string& line)
    {
        if (lines.size() >= maxQueuedLines)
        {
            lines.pop_front();
        }
        lines.push_back(line);
        // Wake the handler if it is waiting for data.
        signal.cancel();
    }

    // Waits until at least one line is queued, then returns it.
    net::awaitable<std::string> next()
    {
        while (lines.empty())
        {
            signal.expires_at(boost::asio::steady_timer::time_point::max());
            boost::system::error_code ec;
            co_await signal.async_wait(
                net::redirect_error(net::use_awaitable, ec));
        }
        auto line = std::move(lines.front());
        lines.pop_front();
        co_return line;
    }

  private:
    std::deque<std::string> lines;
    boost::asio::steady_timer signal;
};

// Connected SSE clients. The server is single threaded, so no locking.
std::unordered_set<std::shared_ptr<Subscriber>> subscribers;
bool journalRunning = false;

// Removes the subscriber when its connection handler exits for any reason.
struct SubscriberGuard
{
    std::shared_ptr<Subscriber> subscriber;
    ~SubscriberGuard()
    {
        subscribers.erase(subscriber);
    }
};

void notifySubscribers(const std::string& line)
{
    for (const auto& sub : subscribers)
    {
        sub->push(line);
    }
}
net::awaitable<void> monitorJournal(boost::asio::io_context& io_context)
{
    namespace bp = boost::process;
    bp::async_pipe pipe_out(io_context);

    // Start journalctl process
    bp::child c("/usr/bin/journalctl", "-f", bp::std_out > pipe_out);
    boost::asio::streambuf buf;
    while (c.running())
    {
        boost::system::error_code ec{};

        // One journal entry per SSE event, so read whole lines.
        auto size = co_await net::async_read_until(
            pipe_out, buf, '\n', net::redirect_error(net::use_awaitable, ec));
        if (ec)
        {
            if (ec != net::error::eof)
            {
                LOG_INFO("Error: {}", ec.message());
            }
            break;
        }

        auto begin = net::buffers_begin(buf.data());
        std::string line(begin, begin + size - 1);
        buf.consume(size);

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
        auto [cert, key] =
            getArgs(parseCommandline(argc, argv), "--cert,-c", "--key,-k");

        boost::asio::io_context io_context;

        boost::asio::ssl::context ssl_context(
            boost::asio::ssl::context::sslv23);

        // Load server certificate and private key
        ssl_context.set_options(boost::asio::ssl::context::default_workarounds |
                                boost::asio::ssl::context::no_sslv2 |
                                boost::asio::ssl::context::single_dh_use);
        std::cerr << "Cert Loading: \n";
        std::string certFile = "/etc/ssl/certs/https/server.mtls.pem";
        std::string keyFile = "/etc/ssl/private/server.mtls.key";
        if (cert)
        {
            certFile = std::string(*cert);
        }
        if (key)
        {
            keyFile = std::string(*key);
        }
        ssl_context.use_certificate_chain_file(certFile);
        ssl_context.use_private_key_file(keyFile,
                                         boost::asio::ssl::context::pem);
        std::cerr << "Cert Loaded: \n";
        HttpRouter router;
        router.setIoContext(io_context);
        TcpStreamType acceptor(io_context.get_executor(), 18080, ssl_context);
        HttpServer server(io_context, acceptor, router);
        // GET /subscribe turns the connection into a long-lived SSE stream.
        // Journal lines are sent back on this same connection as events.
        router.add_sse_handler(
            "/subscribe",
            [&](Request& req, const http_function& params,
                SseWriter writer) -> net::awaitable<void> {
                LOG_INFO("Subscribe request from {}",
                         params.endpoint().address().to_string());
                auto sub = std::make_shared<Subscriber>(
                    co_await net::this_coro::executor);
                subscribers.insert(sub);
                SubscriberGuard guard{sub};
                startJournalMonitor(io_context);

                while (true)
                {
                    auto line = co_await sub->next();
                    if (!co_await writer.write(line))
                    {
                        LOG_INFO("Subscriber {} disconnected",
                                 params.endpoint().address().to_string());
                        break;
                    }
                }
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
