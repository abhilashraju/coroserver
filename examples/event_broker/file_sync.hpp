#pragma once
#include "eventmethods.hpp"
#include "eventqueue.hpp"
#include "file_watcher.hpp"
#include "logger.hpp"
struct FileSync
{
    FileSync(net::any_io_executor io_context, EventQueue& eventQueue,
             const nlohmann::json& json) :
        watcher(io_context), eventQueue(eventQueue)
    {
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
        root = json.value("root", std::string{});
        for (std::string path : json["paths"])
        {
            addPath(path);
        }
    }
    void addPath(const std::string& path)
    {
        watcher.addToWatchRecursive(path);
    }
    void operator()(const std::string& path, FileWatcher::FileStatus status)
    {
        switch (status)
        {
            case FileWatcher::FileStatus::created:
                // eventQueue.addEvent(std::format("FileModified:{}\r\n",
                // path));
                break;
            case FileWatcher::FileStatus::modified:
            {
                auto event = makeEvent("FileModified", path);
                if (!eventQueue.eventExists(event))
                {
                    eventQueue.addEvent(event);
                }
            }

            break;
            case FileWatcher::FileStatus::erased:
            {
                eventQueue.addEvent(makeEvent("FileDeleted", path));
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
            auto path = event.substr(event.find(':') + 1);
            co_await sendHeader(streamer, std::format("Fetch:{}", path));

            co_return co_await recieveFile(streamer, root + path);
        }
        co_return boost::system::error_code{};
    }

    ~FileSync() {}
    FileWatcher watcher;
    EventQueue& eventQueue;
    std::string root;
};
