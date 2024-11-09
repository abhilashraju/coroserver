#include "when_all.hpp"

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <iostream>
#include <tuple>
using namespace std::chrono_literals;
net::awaitable<int> async_task1()
{
    auto executor = co_await net::this_coro::executor;
    net::steady_timer timer(executor, 1s);
    co_await timer.async_wait(net::use_awaitable);
    std::cout << "Task 1 completed" << std::endl;
    co_return 42;
}

net::awaitable<std::string> async_task2()
{
    auto executor = co_await net::this_coro::executor;
    net::steady_timer timer(executor, 2s);
    co_await timer.async_wait(net::use_awaitable);
    std::cout << "Task 2 completed" << std::endl;
    co_return "Hello, World!";
}

net::awaitable<std::string> async_task3()
{
    auto executor = co_await net::this_coro::executor;
    net::steady_timer timer(executor, 3s);
    co_await timer.async_wait(net::use_awaitable);
    std::cout << "Task 3 completed" << std::endl;
    co_return "Goodbye, World!";
}

template <typename... Awaitables>
net::awaitable<void> wait_for(net::io_context& ioc, Awaitables... awaitables)
{
    auto [val1, val2, val3] = co_await when_all(ioc, std::move(awaitables)...);
    std::cout << "Value1: " << val1 << ", Value2: " << val2
              << ", Value3: " << val3 << std::endl;
}

int main()
{
    net::io_context ioc;

    co_spawn(ioc,
             wait_for(ioc, std::move(async_task1), std::move(async_task2),
                      std::move(async_task3)),
             net::detached);
    ioc.run();

    return 0;
}
