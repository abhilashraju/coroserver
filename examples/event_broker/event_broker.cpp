

#include "command_line_parser.hpp"
#include "dbus_sync.hpp"
#include "eventmethods.hpp"
#include "eventqueue.hpp"
#include "file_sync.hpp"
#include "file_watcher.hpp"
#include "logger.hpp"

#include <nlohmann/json.hpp>

#include <csignal>
EventQueue* eventQueue{nullptr};
void signalHandler(int signal)
{
    if (signal == SIGTERM || signal == SIGINT)
    {
        LOG_INFO("Termination signal received, storing event queue...");
        if (eventQueue)
        {
            eventQueue->store();
        }
        exit(0);
    }
}

void setupSignalHandlers()
{
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGINT, signalHandler);
}

net::awaitable<boost::system::error_code>
    hiProvider(Streamer streamer, const std::string& eventReplay)
{
    LOG_DEBUG("Received event: {}", eventReplay);
    co_await sendHeader(streamer, "I am good");
    co_return boost::system::error_code{};
}
net::awaitable<boost::system::error_code>
    hiConsumer(Streamer streamer, const std::string& eventReplay)
{
    LOG_DEBUG("Received event: {}", eventReplay);
    co_await sendHeader(streamer, "How are you?");
    auto [ec, message] = co_await readHeader(streamer);
    LOG_DEBUG("Received event: {}", message);
    co_return boost::system::error_code{};
}

int main(int argc, const char* argv[])
{
    auto [conf] = getArgs(parseCommandline(argc, argv), "--conf,-c");
    if (!conf)
    {
        LOG_ERROR(
            "No config file provided :eg event_broker --conf /path/to/conf");

        return 1;
    }
    auto json = nlohmann::json::parse(std::ifstream(conf.value().data()));

    auto dest = json.value("remote", std::string{});
    auto cert = json.value("cert", std::string{});
    auto rp = json.value("remote-port", std::string{});
    auto port = json.value("port", std::string{});
    auto maxConnections = json.value("max-connections", 1);

    reactor::Logger<std::ostream>& logger = reactor::getLogger();
    logger.setLogLevel(reactor::LogLevel::INFO);
    net::io_context io_context;
    auto conn = std::make_shared<sdbusplus::asio::connection>(io_context);
    ssl::context ssl_server_context(ssl::context::sslv23_server);

    // Load server certificate and private key
    ssl_server_context.set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::single_dh_use);
    if (!cert.empty())
    {
        ssl_server_context.use_certificate_chain_file(
            cert + std::string("/server-cert.pem"));
        ssl_server_context.use_private_key_file(
            cert + std::string("/server-key.pem"),
            boost::asio::ssl::context::pem);
    }
    ssl::context ssl_client_context(ssl::context::sslv23_client);
    TcpStreamType acceptor(io_context.get_executor(), std::atoi(port.data()),
                           ssl_server_context);
    EventQueue eventQueue(io_context.get_executor(), acceptor,
                          ssl_client_context, dest, rp, maxConnections);

    FileSync fileSync(io_context.get_executor(), eventQueue,
                      json.value("file-sync", nlohmann::json{}));

    DbusSync dbusSync(*conn, eventQueue,
                      json.value("dbus-sync", nlohmann::json{}));

    eventQueue.addEventProvider("Hi", hiProvider);
    eventQueue.addEventConsumer("Hi", hiConsumer);

    std::vector<std::string> events{makeEvent("Hello", "World"),
                                    makeEvent("Hi", "World")};
    for (auto& event : events)
    {
        eventQueue.addEvent(event);
    }
    eventQueue.load();
    setupSignalHandlers();
    io_context.run();
    return 0;
}
