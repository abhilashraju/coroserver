#include "command_line_parser.hpp"
#include "file_watcher.hpp"
#include "tcp_server.hpp"
struct SyncHandler
{
    using TcpReader = TcpServer<TcpStreamType, SyncHandler>::Reader;
    using TcpWriter = TcpServer<TcpStreamType, SyncHandler>::Writer;
    auto stringSplitter(char delim = '/')
    {
        return std::views::split(delim) | std::views::transform([](auto&& sub) {
                   return std::string(sub.begin(), sub.end());
               });
    }

    SyncHandler(FileWatcher& watcher) : watcher_(watcher) {}
    void operator()(const std::string& path, FileWatcher::FileStatus status)
    {
        std::map<FileWatcher::FileStatus, std::string> status_map{
            {FileWatcher::FileStatus::created, "created"},
            {FileWatcher::FileStatus::modified, "modified"},
            {FileWatcher::FileStatus::erased, "erased"}};
        std::cout << path << " " << status_map[status] << std::endl;
    }
    net::awaitable<boost::system::error_code>
        parseAndHandle(std::string_view header)
    {
        auto command = header | stringSplitter(':');
        std::vector command_vec(command.begin(), command.end());
        if (command_vec.size() < 2)
        {
            LOG_ERROR("Invalid command: {}", header.data());
            co_return boost::system::errc::parse_error;
        }
        auto handler_it = handler_table.find(command_vec[0]);
        if (handler_it != handler_table.end())
        {
            co_await handler_it->second(command_vec[1]);
            co_return boost::system::erro_code{};
        }
        co_return boost::system::errc::not_found;
    }

    net::awaitable<void> operator()(auto reader, auto writer)
    {
        std::array<char, 1024> data{0};
        auto [ec, bytes] = co_await reader.readUntil(net::buffer(data), "\r\n");
        if (ec)
        {
            std::cerr << "Error reading: " << ec.message() << std::endl;
            co_return;
        }
        std::string_view data_view(data.data(), bytes);
        auto ec = co_await parseAndHandle(data_view);
    }

  private:
    FileWatcher& watcher_;
    using command_handler = std::function<net::awaitable<void>(
        const std::string&, TcpReader, TcpWriter)>;
    std::map<std::string, command_handler> handler_table;
};
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
