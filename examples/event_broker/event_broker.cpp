

#include "command_line_parser.hpp"
#include "eventqueue.hpp"
#include "file_watcher.hpp"
#include "logger.hpp"

net::awaitable<boost::system::error_code>
    helloProvider(Streamer streamer, const std::string& eventReplay)
{
    LOG_DEBUG("Received event: {}", eventReplay);

    co_return boost::system::error_code{};
}
struct FileSync
{
    FileSync(net::any_io_executor io_context, const std::string& path,
             EventQueue& eventQueue) :
        watcher(io_context), eventQueue(eventQueue)
    {
        watcher.addToWatchRecursive(path);
        eventQueue.addEventProvider(
            "FileModified",
            [this](Streamer streamer, const std::string& eventReplay)
                -> net::awaitable<boost::system::error_code> {
                co_return co_await providerHandler(streamer, eventReplay);
            });
        eventQueue.addEventProvider(
            "FileDeleted",
            [this](Streamer streamer, const std::string& eventReplay)
                -> net::awaitable<boost::system::error_code> {
                co_return co_await providerHandler(streamer, eventReplay);
            });
        eventQueue.addEventConsumer(
            "FileModified",
            [this](Streamer streamer, const std::string& event)
                -> net::awaitable<boost::system::error_code> {
                co_return co_await fileConsumer(streamer, event);
            });
        eventQueue.addEventConsumer(
            "FileDeleted",
            [this](Streamer streamer, const std::string& event)
                -> net::awaitable<boost::system::error_code> {
                co_return co_await fileConsumer(streamer, event);
            });
        boost::asio::co_spawn(io_context, watchFileChanges(watcher, *this),
                              boost::asio::detached);
    }
    void operator()(const std::string& path, FileWatcher::FileStatus status)
    {
        switch (status)
        {
            case FileWatcher::FileStatus::created:
            case FileWatcher::FileStatus::modified:
                eventQueue.addEvent(std::format("FileModified:{}\r\n", path));
                break;
            case FileWatcher::FileStatus::erased:
            {
                eventQueue.addEvent(std::format("FileDeleted:{}\r\n", path));
            }
            break;
        }
    }
    net::awaitable<boost::system::error_code>
        providerHandler(Streamer streamer, const std::string& eventReplay) const
    {
        if (eventReplay.find("Fetch") != std::string::npos)
        {
            auto path = eventReplay.substr(eventReplay.find(':') + 1);
            co_return co_await sendFile(streamer, path);
        }
        co_return boost::system::error_code{};
    }
    net::awaitable<boost::system::error_code>
        fileConsumer(Streamer streamer, const std::string& event) const
    {
        if (event.find("FileModified") != std::string::npos)
        {
            std::string root = "/tmp";
            auto path = event.substr(event.find(':') + 1);
            co_await sendHeader(streamer, std::format("Fetch:{}", path));

            co_return co_await recieveFile(streamer, root + path);
        }
        co_return boost::system::error_code{};
    }

    ~FileSync() {}
    FileWatcher watcher;
    EventQueue& eventQueue;
};
int main(int argc, const char* argv[])
{
    auto [cert, path, dest, rp] =
        getArgs(parseCommandline(argc, argv), "--cert,-c", "--dir,-d",
                "--remote,-r", "--remote-port,-rp");
    reactor::Logger<std::ostream>& logger = reactor::getLogger();
    logger.setLogLevel(reactor::LogLevel::INFO);
    net::io_context io_context;
    ssl::context ssl_server_context(ssl::context::sslv23_server);

    // Load server certificate and private key
    ssl_server_context.set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::single_dh_use);
    if (cert)
    {
        ssl_server_context.use_certificate_chain_file(
            cert.value().data() + std::string("/server-cert.pem"));
        ssl_server_context.use_private_key_file(
            cert.value().data() + std::string("/server-key.pem"),
            boost::asio::ssl::context::pem);
    }
    else
    {
        ssl_server_context.use_certificate_chain_file("/tmp/server-cert.pem");
        ssl_server_context.use_private_key_file("/tmp/server-key.pem",
                                                boost::asio::ssl::context::pem);
    }
    ssl::context ssl_client_context(ssl::context::sslv23_client);
    TcpStreamType acceptor(io_context.get_executor(), 8080, ssl_server_context);
    EventQueue eventQueue(io_context.get_executor(), acceptor,
                          ssl_client_context, dest.value().data(),
                          rp.value().data());
    FileSync fileSync(io_context.get_executor(), path.value().data(),
                      eventQueue);
    eventQueue.addEventProvider("Hello", helloProvider);

    std::vector<std::string> events{"Hello: World\r\n"};
    for (auto& event : events)
    {
        eventQueue.addEvent(event);
    }
    io_context.run();
    return 0;
}
