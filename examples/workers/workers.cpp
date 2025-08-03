#include "boost/url.hpp"
#include "command_line_parser.hpp"
#include "logger.hpp"
#include "worker.hpp"

#include <vector>
struct MyStruct
{
    int value;
    MyStruct() : value(0) {}
    MyStruct(int v) : value(v) {}
    MyStruct(const MyStruct& other) : value(other.value)
    {
        LOG_INFO("MyStruct with value {} is being copied", value);
    }
    MyStruct(MyStruct&& other) noexcept : value(other.value)
    {
        LOG_INFO("MyStruct with value {} is being moved", value);
        other.value = 0; // Reset the moved-from object
    }
    ~MyStruct()
    {
        LOG_INFO("MyStruct with value {} is being destroyed", value);
    }
};
int main(int argc, const char* argv[])
{
    reactor::getLogger().setLogLevel(reactor::LogLevel::DEBUG);

    net::io_context ioc;
    LOG_INFO("Main Thread ID: {}", std::this_thread::get_id());

    auto myTask = []() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        LOG_INFO("Task completed in thread ID: {}", std::this_thread::get_id());
        return MyStruct(10);
    };
    net::co_spawn(
        ioc,
        [myTask = std::move(myTask), &ioc]() -> net::awaitable<void> {
            LOG_INFO("Spawning task in the worker pool");
            auto [ec,
                  ret] = co_await asyncCall<MyStruct>(ioc, std::move(myTask));
            if (ec)
            {
                LOG_ERROR("Error executing task: {}", ec.message());
            }
            else
            {
                LOG_INFO(
                    "Task executed successfully in {} thread with result: {}",
                    std::this_thread::get_id(), ret.value);
            }
        },
        net::detached);

    ioc.run();
    return 0;
}
