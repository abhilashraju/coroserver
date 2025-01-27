#pragma once
#include "file_watcher.hpp"
#include "tcp_client.hpp"
#include "tcp_server.hpp"
#include "utilities.hpp"
using Streamer = TimedStreamer<ssl::stream<tcp::socket>>;
using COMMAND_HANDLER =
    std::function<net::awaitable<void>(const std::string&, Streamer)>;
inline std::map<std::string, COMMAND_HANDLER>& getHandlerTable()
{
    static std::map<std::string, COMMAND_HANDLER> handler_table;
    return handler_table;
}
inline void addHandler(const std::string& command, COMMAND_HANDLER handler)
{
    getHandlerTable()[command] = handler;
}

inline AwaitableResult<std::string> readHeader(Streamer streamer)
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
inline net::awaitable<boost::system::error_code>
    parseAndHandle(std::string_view header, Streamer streamer)
{
    auto command = header | stringSplitter(':');
    std::vector<std::string> command_vec(command.begin(), command.end());
    if (command_vec.size() < 2)
    {
        LOG_ERROR("Invalid command: {}", header.data());
        co_return boost::system::errc::make_error_code(
            boost::system::errc::invalid_argument);
    }
    auto handler_it = getHandlerTable().find(command_vec[0]);
    if (handler_it != getHandlerTable().end())
    {
        co_await handler_it->second(command_vec[1], streamer);
        co_return boost::system::error_code{};
    }
    co_return boost::system::errc::make_error_code(
        boost::system::errc::invalid_argument);
}
inline net::awaitable<boost::system::error_code> next(Streamer streamer)
{
    auto [ec, data] = co_await readHeader(streamer);
    if (ec)
    {
        co_return ec;
    }
    co_await parseAndHandle(data, streamer);
    co_return boost::system::error_code{};
}
inline net::awaitable<void>
    handleFileModified(const std::string& path, Streamer streamer)
{
    std::string header = std::format("FileModified:{}\r\n", path);
    co_await streamer.write(net::buffer(header));
    co_await next(streamer);
}
inline net::awaitable<void> syncFile(
    const std::string& path, FileWatcher::FileStatus status, Streamer streamer)
{
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

struct SyncHandler
{
    SyncHandler(FileWatcher& watcher) : watcher_(watcher) {}
    void operator()(const std::string& path, FileWatcher::FileStatus status)
    {
        net::co_spawn(
            watcher_.stream_.get_executor(),
            [this, path, status]() -> net::awaitable<void> {
                ssl::context ssl_context(ssl::context::sslv23_client);
                TcpClient client(watcher_.stream_.get_executor(), ssl_context);
                auto ec = co_await client.connect("127.0.0.1", "8080");
                if (ec)
                {
                    LOG_ERROR("Connect error: {}", ec.message());
                    co_return;
                }
                co_await syncFile(path, status, client.streamer());
            },
            net::detached);
    }

    net::awaitable<void> operator()(auto streamer)
    {
        co_await next(streamer);
    }

  private:
    FileWatcher& watcher_;
};
