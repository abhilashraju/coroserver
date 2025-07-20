#include "boost/url.hpp"
#include "command_line_parser.hpp"
#include "logger.hpp"
#include "worker.hpp"

#include <vector>

int main(int argc, const char* argv[])
{
    reactor::getLogger().setLogLevel(reactor::LogLevel::DEBUG);

    net::io_context ioc;
    LOG_INFO("Main Thread ID: {}", std::this_thread::get_id());
    auto myTask = []() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        LOG_INFO("Task completed in thread ID: {}", std::this_thread::get_id());
    };
    net::co_spawn(
        ioc,
        [myTask = std::move(myTask), &ioc]() -> net::awaitable<void> {
            LOG_INFO("Spawning task in the worker pool");
            auto [ec] = co_await asyncCall(ioc, std::move(myTask));
            if (ec)
            {
                LOG_ERROR("Error executing task: {}", ec.message());
            }
            else
            {
                LOG_INFO("Task executed successfully in {} thread",
                         std::this_thread::get_id());
            }
        },
        net::detached);

    ioc.run();
    return 0;
}
