#pragma once
#include "file_watcher.hpp"
#include "tcp_client.hpp"
#include "tcp_server.hpp"
struct SyncHandler
{
    using Streamer = TcpServer<TcpStreamType, SyncHandler>::TimedStreamer;

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
        net::co_spawn(watcher_.stream_.get_executor(),
                      std::bind_front(&syncFile, path, status), net::detached);
    }
    net::awaitable<void> syncFile(const std::string& path,
                                  FileWatcher::FileStatus status)
    {
        std::string command = "sync_file";
        std::string status_str = status_map[status];
        std::string header = command + ":" + status_str;
        ssl::context ssl_context(ssl::context::sslv23_client);
        TcpClient client(watcher_.stream_.get_executor(), ssl_context);
        auto [ec, writer] = co_await client.connect("127.0.0.1", "8080");
        if (ec)
        {
            LOG_ERROR("Connect error: {}", ec.message());
            co_return;
        }
        Reader reader(writer.socket);
        switch (status)
        {
            case FileWatcher::FileStatus::created:
            case FileWatcher::FileStatus::modified:
            {
                std::string header = std::format("FileModified:{}\r\n", path);
                co_await writer.write(net::buffer(header));
                boost::beast::flat_buffer buffer;
                co_await reader.readUntil(buffer, "\r\n");
                std::string(static_cast<const char*>(buffer.data().data()),
                            buffer.size());
                co_await parseAndHandle(data_view, reader, writer);
            }
            break;
            case FileWatcher::FileStatus::erased:
            {
                std::string header = std::format("FileErased:{}\r\n", path);
                co_await writer.write(net::buffer(header));
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
        boost::beast::flat_buffer buffer;
        auto [ec, bytes] = co_await streamer.readUntil(buffer, "\r\n");
        if (ec)
        {
            LOG_DEBUG("Error reading: {}", ec.message());
            co_return;
        }
        std::string data_view(static_cast<const char*>(buffer.data().data()),
                              bytes);
        LOG_DEBUG("Received: {}", data_view);
        co_await parseAndHandle(data_view, streamer);
    }
    void addHandler(const std::string& command, COMMAND_HANDLER handler)
    {
        handler_table[command] = handler;
    }

  private:
    FileWatcher& watcher_;
    std::map<std::string, COMMAND_HANDLER> handler_table;
};
