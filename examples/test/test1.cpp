#include "file_watcher.hpp"
#include "logger.hpp"
#include "utilities.hpp"

#include <nlohmann/json.hpp>
namespace net = boost::asio;
int main()
{
    auto& logger = reactor::getLogger();
    logger.setLogLevel(reactor::LogLevel::INFO);
    net::io_context io_context;
    FileWatcher watcher(io_context.get_executor());
    watcher.addToWatchRecursive("/workspace/public/coroserver/build");
    auto func = [](const std::string& path, FileWatcher::FileStatus status) {
        switch (status)
        {
            case FileWatcher::FileStatus::created:
                LOG_INFO("File: {} Status: {}", path, "created");
                break;
            case FileWatcher::FileStatus::modified:
                LOG_INFO("File: {} Status: {}", path, "modified");
                break;
            case FileWatcher::FileStatus::erased:
            {
                LOG_INFO("File: {} Status: {}", path, "erased");
            }
            break;
        }
    };
    boost::asio::co_spawn(io_context, watchFileChanges(watcher, func),
                          boost::asio::detached);
    io_context.run();
}
