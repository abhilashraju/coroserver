#pragma once
#include "file_watcher.hpp"
#include "tcp_client.hpp"
#include "tcp_server.hpp"
inline AwaitableResult<std::string> readHeader(auto streamer)
{
    auto [ec, data] = co_await streamer.readUntil("\r\n");
    if (ec)
    {
        LOG_ERROR("Error reading: {}", ec.message());
        co_return std::make_pair(ec, data);
    }
    data.erase(data.length() - 2, 2);
    co_return std::make_pair(ec, data);
}
struct SyncHandler
{
    using Streamer = TcpServer<TcpStreamType, SyncHandler>::Streamer;

    using COMMAND_HANDLER =
        std::function<net::awaitable<void>(const std::string&, Streamer)>;
    auto stringSplitter(char delim = '/')
    {
        return std::views::split(delim) | std::views::transform([](auto&& sub) {
                   return std::string(sub.begin(), sub.end());
               });
    }

    SyncHandler(FileWatcher& watcher) : watcher_(watcher) {}
    void operator()(const std::string& path, FileWatcher::FileStatus status)
    {
        net::co_spawn(
            watcher_.stream_.get_executor(),
            [this, path, status]() -> net::awaitable<void> {
                co_await syncFile(path, status);
            },
            net::detached);
    }
    net::awaitable<void> handleFileModified(const std::string& path,
                                            Streamer streamer)
    {
        std::string header = std::format("FileModified:{}\r\n", path);
        co_await streamer.write(net::buffer(header));
        auto [ec, buffer] = co_await readHeader(streamer);
        if (ec)
        {
            LOG_ERROR("Error reading: {}", ec.message());
            co_return;
        }
        co_await parseAndHandle(buffer, streamer);
    }
    net::awaitable<void> syncFile(const std::string& path,
                                  FileWatcher::FileStatus status)
    {
        ssl::context ssl_context(ssl::context::sslv23_client);
        TcpClient client(watcher_.stream_.get_executor(), ssl_context);
        auto ec = co_await client.connect("127.0.0.1", "8080");
        if (ec)
        {
            LOG_ERROR("Connect error: {}", ec.message());
            co_return;
        }
        auto streamer = client.streamer();
        switch (status)
        {
            case FileWatcher::FileStatus::created:
            case FileWatcher::FileStatus::modified:
                co_await handleFileModified(path, streamer);
                break;
            case FileWatcher::FileStatus::erased:
            {
                std::string header = std::format("FileErased:{}\r\n", path);
                co_await streamer.write(net::buffer(header));
            }
            break;
        }
    }

    net::awaitable<boost::system::error_code>
        parseAndHandle(std::string_view header, auto streamer)
    {
        auto command = header | stringSplitter(':');
        std::vector command_vec(command.begin(), command.end());
        if (command_vec.size() < 2)
        {
            LOG_ERROR("Invalid command: {}", header.data());
            co_return boost::system::errc::make_error_code(
                boost::system::errc::invalid_argument);
        }
        auto handler_it = handler_table.find(command_vec[0]);
        if (handler_it != handler_table.end())
        {
            co_await handler_it->second(command_vec[1], streamer);
            co_return boost::system::error_code{};
        }
        co_return boost::system::errc::make_error_code(
            boost::system::errc::invalid_argument);
    }

    net::awaitable<void> operator()(auto streamer)
    {
        auto [ec, data] = co_await readHeader(streamer);
        if (ec)
        {
            co_return;
        }
        co_await parseAndHandle(data, streamer);
    }
    void addHandler(const std::string& command, COMMAND_HANDLER handler)
    {
        handler_table[command] = handler;
    }

  private:
    FileWatcher& watcher_;
    std::map<std::string, COMMAND_HANDLER> handler_table;
};
