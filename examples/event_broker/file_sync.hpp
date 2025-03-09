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
            "ArchiveModified",
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
            "ArchiveModified",
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
        // boost::asio::co_spawn(io_context, watchFileChanges(watcher, *this),
        //                       boost::asio::detached);
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
    net::awaitable<boost::system::error_code> providerHandler(
        Streamer streamer, const std::string& event) const
    {
        auto [id, path] = parseEvent(event);
        if (id == "Fetch")
        {
            co_return co_await sendFile(streamer, path);
        }
        if (id == "FetchArchive")
        {
            std::string archpath = "/tmp/.archive/";
            if (!fs::exists(archpath))
            {
                fs::create_directories(archpath);
            }
            replaced(path, '/', '_', std::back_inserter(archpath));
            archpath += ".tar.gz";

            if (createTarArchive(path, archpath))
            {
                co_await sendHeader(
                    streamer, makeEvent("FileType", "archive:" + archpath));
                auto ec = co_await sendFile(streamer, archpath);
                std::filesystem::remove(archpath);
                co_return ec;
            }
            co_await sendHeader(streamer, makeEvent("FileNotFound", path));
            co_return boost::system::error_code{};
        }
        co_return boost::system::error_code{};
    }
    net::awaitable<boost::system::error_code> fileConsumer(
        Streamer streamer, const std::string& event) const
    {
        auto [id, data] = parseEvent(event);
        if (id == "FileModified")
        {
            co_await sendHeader(streamer, std::format("Fetch:{}", data));
            co_return co_await recieveFile(streamer, root, data);
        }
        if (id == "FileDeleted")
        {
            co_return co_await deleteFile(data);
        }
        if (id == "ArchiveModified")
        {
            co_await sendHeader(streamer, std::format("FetchArchive:{}", data));
            co_return co_await recieveFile(streamer, root, data);
        }
        LOG_ERROR("Unknown event: {}", event);
        co_return boost::system::error_code{
            boost::system::errc::operation_not_supported,
            boost::system::system_category()};
    }
    net::awaitable<boost::system::error_code> deleteFile(
        const std::string& path) const
    {
        try
        {
            std::filesystem::remove(root + path);
        }
        catch (std::exception& e)
        {
            LOG_ERROR("Error deleting file: {}", e.what());
            co_return boost::system::error_code{
                1, boost::system::system_category()};
        }
        co_return boost::system::error_code{};
    }

    ~FileSync() {}
    FileWatcher watcher;
    EventQueue& eventQueue;
    std::string root;
};
