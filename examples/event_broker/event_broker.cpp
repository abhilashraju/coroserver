

#include "command_line_parser.hpp"
#include "eventqueue.hpp"
#include "file_watcher.hpp"
#include "logger.hpp"

#include <filesystem>
#include <fstream>
namespace fs = std::filesystem;
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
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                LOG_ERROR("File not found: {}", path);
                auto [ec,
                      size] = co_await sendHeader(streamer, "FileNotFound:");
                co_return ec ? ec : boost::system::error_code{};
            }
            file.seekg(0, std::ios::end);
            auto fileSize = static_cast<std::size_t>(file.tellg());
            file.seekg(0, std::ios::beg);

            auto [ec, size] = co_await sendHeader(
                streamer, std::format("Content-Size:{}", fileSize));
            if (ec)
            {
                LOG_ERROR("Failed to write to stream: {}", ec.message());
                co_return ec;
            }
            std::array<char, 1024> data;
            while (true)
            {
                file.read(data.data(), data.size());
                if (file.eof())
                {
                    co_await sendData(streamer,
                                      net::buffer(data, file.gcount()));
                    break;
                }
                co_await sendData(streamer, net::buffer(data));
            }
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

            auto [ec, header] = co_await readHeader(streamer);
            if (header.find("FileNotFound") != std::string::npos)
            {
                LOG_ERROR("File not found: {}", path);
                co_return boost::system::error_code{};
            }
            if (header.find("Content-Size") != std::string::npos)
            {
                auto size = std::stoul(header.substr(header.find(':') + 1));
                fs::path filePath = root + path;
                if (!fs::exists(std::filesystem::path(filePath).parent_path()))
                {
                    fs::create_directories(
                        std::filesystem::path(filePath).parent_path());
                }
                std::ofstream file(filePath, std::ios::binary);
                if (!file.is_open())
                {
                    LOG_ERROR("File not found: {}", filePath.string());
                    co_return boost::system::error_code{};
                }
                std::array<char, 1024> data;
                while (size > 0)
                {
                    auto [ec, bytes] =
                        co_await readData(streamer, net::buffer(data));
                    if (ec)
                    {
                        LOG_ERROR("Failed to read from stream: {}",
                                  ec.message());
                        co_return ec;
                    }
                    file.write(data.data(), bytes);
                    size -= bytes;
                }
            }
        }
        co_return boost::system::error_code{};
    }

    ~FileSync() {}
    FileWatcher watcher;
    EventQueue& eventQueue;
};
int main(int argc, const char* argv[])
{
    auto [cert, path] =
        getArgs(parseCommandline(argc, argv), "--cert,-c", "--dir,-d");
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
                          ssl_client_context, "127.0.0.1", "8080");
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
