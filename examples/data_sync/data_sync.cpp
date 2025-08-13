#include "command_line_parser.hpp"
#include "synchandler.hpp"

#include <filesystem>
#include <fstream>
using namespace NSNAME;
namespace fs = std::filesystem;
net::awaitable<void> fileDownloadHandler(const std::string& path,
                                         Streamer streamer)
{
    fs::path filePath = "/tmp" + path;
    if (!fs::exists(std::filesystem::path(filePath).parent_path()))
    {
        fs::create_directories(std::filesystem::path(filePath).parent_path());
    }
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        LOG_ERROR("File not found: {}", filePath.string());
        co_return;
    }
    std::string header = std::format("Continue:{}\r\n", path);
    co_await streamer.write(net::buffer(header));
    std::array<char, 1024> data{0};
    while (true)
    {
        auto [ec, bytes] =
            co_await streamer.read(net::buffer(data.data(), data.size()));
        if (ec)
        {
            if (ec != boost::asio::error::eof)
            {
                LOG_ERROR("Error reading: {}", ec.message());
            }
            break;
        }
        file.write(data.data(), bytes);
        std::fill(data.begin(), data.end(), 0);
    }
    file.close();
}
net::awaitable<void> fileContinueHandler(const std::string& path,
                                         Streamer streamer)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        LOG_ERROR("File not found: {}", path);
        co_return;
    }
    std::array<char, 1024> data;
    while (true)
    {
        file.read(data.data(), data.size());
        if (file.eof())
        {
            co_await streamer.write(net::buffer(data.data(), file.gcount()));
            break;
        }
        co_await streamer.write(net::buffer(data));
    }
}
int main(int argc, const char* argv[])
{
    try
    {
        reactor::getLogger().setLogLevel(reactor::LogLevel::DEBUG);
        auto [path, port] =
            getArgs(parseCommandline(argc, argv), "--dir,-d", "--port,-p");
        if (!path.has_value() || !port.has_value())
        {
            std::cerr
                << "Usage: data_sync --path <directory path> --port <port no>\n";
            return 1;
        }
        boost::asio::io_context io_context;
        boost::asio::ssl::context ssl_context(
            boost::asio::ssl::context::sslv23);

        // Load server certificate and private key
        ssl_context.set_options(boost::asio::ssl::context::default_workarounds |
                                boost::asio::ssl::context::no_sslv2 |
                                boost::asio::ssl::context::single_dh_use);
        std::cerr << "Cert Loading: \n";
        ssl_context.use_certificate_chain_file(
            "/etc/ssl/private/server-cert.pem");
        ssl_context.use_private_key_file("/etc/ssl/private/server-key.pem",
                                         boost::asio::ssl::context::pem);
        std::cerr << "Cert Loaded: \n";

        FileWatcher watcher(io_context.get_executor());
        watcher.addToWatchRecursive(path.value().data());

        SyncHandler syncHandler(watcher, "127.0.0.1", "8080");
        addHandler("FileModified", fileDownloadHandler);
        addHandler("Continue", fileContinueHandler);

        TcpStreamType acceptor(io_context.get_executor(),
                               std::atoi(port.value().data()), ssl_context);
        TcpServer server(io_context.get_executor(), acceptor, syncHandler);

        boost::asio::co_spawn(io_context,
                              watchFileChanges(watcher, syncHandler),
                              boost::asio::detached);

        io_context.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
