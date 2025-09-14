#pragma once
#include "name_space.hpp"

#include <sys/inotify.h>
#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <reactor/logger.hpp>

#include <coroutine>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
namespace NSNAME
{
struct FileWatcher
{
    enum class FileStatus
    {
        created,
        modified,
        erased
    };
    using EventMap = std::map<std::string, inotify_event>;
    mutable boost::asio::posix::stream_descriptor stream_;
    std::map<int, std::string> watch_fds;

    FileWatcher(boost::asio::any_io_executor io_context) :
        stream_(io_context, inotify_init1(IN_NONBLOCK))
    {
        if (stream_.native_handle() < 0)
        {
            throw std::runtime_error("Failed to initialize inotify");
        }
    }
    std::string trimmed(const std::string& path)
    {
        std::string trimmed_path = path;
        if (!trimmed_path.empty() && trimmed_path.back() == '/')
        {
            trimmed_path.pop_back();
        }
        return trimmed_path;
    }
    void addToWatch(const std::string& path)
    {
        int wd = inotify_add_watch(stream_.native_handle(), path.c_str(),
                                   std::filesystem::is_directory(path)
                                       ? IN_MODIFY | IN_CREATE | IN_DELETE
                                       : IN_MODIFY);

        if (wd < 0)
        {
            std::string error_message = std::strerror(errno);
            LOG_ERROR("Failed to add inotify watch for path {}: {}", path,
                      error_message);
            return;
        }
        watch_fds[wd] = trimmed(path);
    }
    void addToWatchRecursive(const std::string& path)
    {
        if (!std::filesystem::exists(path))
        {
            LOG_ERROR("Path does not exist or is not a directory: {}", path);
            return;
        }
        addToWatch(path);
        if (std::filesystem::is_directory(path))
        {
            for (const auto& entry :
                 std::filesystem::recursive_directory_iterator(path))
            {
                if (entry.is_directory())
                {
                    addToWatch(entry.path().string());
                }
            }
        }
    }
    ~FileWatcher()
    {
        for (auto [wd, p] : watch_fds)
        {
            inotify_rm_watch(stream_.native_handle(), wd);
        }
    }

    boost::asio::awaitable<EventMap> watch() const
    {
        std::vector<unsigned char> buffer(1024);
        std::size_t length = co_await stream_.async_read_some(
            boost::asio::buffer(buffer), boost::asio::use_awaitable);

        EventMap events;
        for (std::size_t i = 0; i < length;)
        {
            inotify_event* event = reinterpret_cast<inotify_event*>(&buffer[i]);
            auto path = watch_fds.at(event->wd);
            if (event->len > 0)
            {
                path += std::string("/") + event->name;
            }
            events[path] = *event;
            i += sizeof(inotify_event) + event->len;
        }

        co_return events;
    }
    boost::asio::awaitable<void> watchForChanges(auto& handler) const
    {
        auto events = co_await watch();
        for (auto& [name, event] : events)
        {
            LOG_INFO("File: {} Status: {}", name, event.mask);
            if (event.mask & IN_CREATE)
            {
                handler(name, FileStatus::created);
            }
            else if (event.mask & IN_DELETE)
            {
                handler(name, FileStatus::erased);
            }
            else if (event.mask & IN_MODIFY)
            {
                handler(name, FileStatus::modified);
            }
        }
        boost::asio::co_spawn(
            stream_.get_executor(),
            [this, &handler]() -> boost::asio::awaitable<void> {
                co_await watchForChanges(handler);
            },
            boost::asio::detached);
        co_return;
    }
};
template <typename Handler>
concept FileChangeHandler =
    requires(Handler h, std::string path, FileWatcher::FileStatus status) {
        { h(path, status) } -> std::same_as<void>;
    };
template <FileChangeHandler Handler>
inline boost::asio::awaitable<void> watchFileChanges(FileWatcher& watcher,
                                                     Handler& handler)
{
    co_await watcher.watchForChanges(handler);
}
} // namespace NSNAME
