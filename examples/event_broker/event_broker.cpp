

#include "archive.hpp"
#include "command_line_parser.hpp"
#include "dbus_sync.hpp"
#include "eventmethods.hpp"
#include "eventqueue.hpp"
#include "file_sync.hpp"
#include "file_watcher.hpp"
#include "logger.hpp"
#include "shared_library.hpp"

#include <nlohmann/json.hpp>

#include <csignal>
EventQueue* peventQueue{nullptr};
void signalHandler(int signal)
{
    if (signal == SIGTERM || signal == SIGINT)
    {
        LOG_INFO("Termination signal received, storing event queue...");
        if (peventQueue)
        {
            peventQueue->store();
        }
        exit(0);
    }
}

void setupSignalHandlers()
{
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGINT, signalHandler);
}

net::awaitable<boost::system::error_code> hiProvider(
    Streamer streamer, const std::string& eventReplay)
{
    LOG_DEBUG("Received event: {}", eventReplay);
    co_await sendHeader(streamer, "I am good");
    co_return boost::system::error_code{};
}
void addFiletoUpdateRecursive(const std::string& path)
{
    if (!std::filesystem::exists(path))
    {
        return;
    }
    if (std::filesystem::is_directory(path))
    {
        peventQueue->addEvent(makeEvent("ArchiveModified", path));
    }
    else
    {
        peventQueue->addEvent(makeEvent("FileModified", path));
    }
}
net::awaitable<boost::system::error_code> fullSync(
    const nlohmann::json& paths, Streamer streamer, const std::string& data)
{
    peventQueue->beginBarrier();
    if (data == "*")
    {
        LOG_DEBUG("Received Event for FullSync: {}", data);
        for (std::string path : paths["paths"])
        {
            addFiletoUpdateRecursive(path);
        }

        co_return boost::system::error_code{};
    }
    if (fs::exists(data))
    {
        addFiletoUpdateRecursive(data);
    }
    peventQueue->endBarrier();
}
net::awaitable<boost::system::error_code> publisher(
    const nlohmann::json& paths, Streamer streamer, const std::string& event)
{
    LOG_DEBUG("Received Event for publish: {}", event);
    auto [id, data] = parseEvent(event);
    auto [innerID, innerData] = parseEvent(data);
    if (innerID == "FullSync")
    {
        co_return co_await fullSync(paths, streamer, innerData);
    }
    peventQueue->addEvent(makeEvent(data));
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
    try
    {
        auto json = nlohmann::json::parse(std::ifstream(conf.value().data()));

        auto dest = json.value("remote", std::string{});
        auto cert = json.value("cert", std::string{});
        auto privkey = json.value("privkey", std::string{});
        auto rp = json.value("remote-port", std::string{});
        auto port = json.value("port", std::string{});
        auto maxConnections = json.value("max-connections", 1);
        auto pluginfolder = json.value("plugins-folder", "/usr/bin/plugins");

        auto& logger = reactor::getLogger();
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
            ssl_server_context.use_certificate_chain_file(cert);
            ssl_server_context.use_private_key_file(
                privkey, boost::asio::ssl::context::pem);
        }
        ssl::context ssl_client_context(ssl::context::sslv23_client);
        TcpStreamType acceptor(io_context.get_executor(),
                               std::atoi(port.data()), ssl_server_context);
        EventQueue eventQueue(io_context.get_executor(), acceptor,
                              ssl_client_context, dest, rp, maxConnections);
        peventQueue = &eventQueue;
        FileSync fileSync(io_context.get_executor(), eventQueue,
                          json.value("file-sync", nlohmann::json{}));

        DbusSync dbusSync(*conn, eventQueue,
                          json.value("dbus-sync", nlohmann::json{}));

        // eventQueue.addEventProvider("Hi", hiProvider);
        eventQueue.addEventConsumer(
            "Publish",
            std::bind_front(publisher,
                            json.value("file-sync", nlohmann::json{})));

        PluginDb db(pluginfolder);
        auto pInterfaces = db.getInterFaces<EventBrokerPlugin>();
        for (auto& plugin : pInterfaces)
        {
            auto providers = plugin->getProviders();
            for (auto& [id, provider] : providers)
            {
                eventQueue.addEventProvider(id, provider);
            }
            auto consumers = plugin->getConsumers();
            for (auto& [id, consumer] : consumers)
            {
                eventQueue.addEventConsumer(id, consumer);
            }
        }

        std::vector<std::string> events{makeEvent("Hello", "World"),
                                        makeEvent("Hi", "World")};
        for (auto& event : events)
        {
            eventQueue.addEvent(event);
        }
        eventQueue.load();
        setupSignalHandlers();
        io_context.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
    }
    return 0;
}
