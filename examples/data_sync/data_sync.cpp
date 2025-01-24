#include "command_line_parser.hpp"
#include "synchandler.hpp"

#include <fstream>
net::awaitable<void> fileDownloadHandler(const std::string& path,
                                         SyncHandler::Streamer streamer)
{
    std::string filePath = "/tmp/" + path;
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        LOG_ERROR("File not found: {}", filePath);
        co_return;
    }
    std::string header = std::format("Continue:{}\r\n", path);
    co_await streamer.write(net::buffer(header));
    std::string data;
    while (true)
    {
        auto [ec, bytes] = co_await streamer.read(net::buffer(data));
        if (ec)
        {
            if (ec != boost::asio::error::eof)
            {
                LOG_ERROR("Error reading: {}", ec.message());
            }
            break;
        }
        file.write(data.data(), bytes);
    }
    file.close();
}
net::awaitable<void> fileContinueHandler(const std::string& path,
                                         SyncHandler::Streamer streamer)
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
        auto [path] = getArgs(parseCommandline(argc, argv), "--path,-p");
        if (!path.has_value())
        {
            std::cerr << "Usage: data_sync --path <directory path>\n";
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

        FileWatcher watcher(io_context);
        watcher.addToWatchRecursive(path.value().data());

        SyncHandler syncHandler(watcher);
        syncHandler.addHandler("FileDownload", fileDownloadHandler);
        syncHandler.addHandler("Continue", fileContinueHandler);

        TcpStreamType acceptor(io_context, 8080, ssl_context);
        TcpServer server(io_context, acceptor, syncHandler);

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
