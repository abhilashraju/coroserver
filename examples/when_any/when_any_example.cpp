#include "logger.hpp"
#include "when_any.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <chrono>
#include <iostream>
#include <stop_token>

namespace net = boost::asio;
using namespace std::chrono_literals;

// Stop-token-aware timer abstraction
template <typename Duration>
net::awaitable<boost::system::error_code> async_wait_with_stop(
    net::any_io_executor exec, Duration duration, std::stop_token stop_token)
{
    auto timer = net::steady_timer(exec);
    timer.expires_after(duration);

    // Poll for stop requests while waiting
    auto start_time = std::chrono::steady_clock::now();
    while (!stop_token.stop_requested())
    {
        // Check periodically
        auto poll_timer = net::steady_timer(exec);
        poll_timer.expires_after(std::chrono::milliseconds(100));
        boost::system::error_code ec;
        co_await poll_timer.async_wait(
            net::redirect_error(net::use_awaitable, ec));

        if (stop_token.stop_requested())
        {
            timer.cancel();
            co_return make_error_code(boost::system::errc::operation_canceled);
        }

        // Check if enough time has elapsed
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= duration)
        {
            break;
        }
    }

    if (stop_token.stop_requested())
    {
        co_return make_error_code(boost::system::errc::operation_canceled);
    }

    co_return boost::system::error_code{};
}

// Stop-token-aware void task factory
auto task_void_factory(int id, std::chrono::seconds delay)
{
    return [id, delay](std::stop_token stop_token) -> net::awaitable<void> {
        std::cout << "Task " << id << " starting with " << delay.count()
                  << "s delay\n";

        auto exec = co_await net::this_coro::executor;
        auto ec = co_await async_wait_with_stop(exec, delay, stop_token);

        if (ec)
        {
            std::cout << "Task " << id << " CANCELLED (error: " << ec.message()
                      << ")\n";
        }
        else
        {
            std::cout << "Task " << id << " completed after " << delay.count()
                      << "s\n";
        }
    };
}

// Stop-token-aware value task factory
auto task_value_factory(int id, std::chrono::seconds delay)
{
    return [id, delay](std::stop_token stop_token) -> net::awaitable<int> {
        std::cout << "Task " << id << " starting with " << delay.count()
                  << "s delay\n";

        auto exec = co_await net::this_coro::executor;
        auto ec = co_await async_wait_with_stop(exec, delay, stop_token);

        if (ec)
        {
            std::cout << "Task " << id << " CANCELLED (error: " << ec.message()
                      << ")\n";
            throw boost::system::system_error(ec, "Task cancelled");
        }

        std::cout << "Task " << id << " completed after " << delay.count()
                  << "s with value " << id * 100 << "\n";
        co_return id * 100;
    };
}

net::awaitable<void> example_void_vector_with_cancellation()
{
    std::cout
        << "\n=== Example 1: when_any with void stop-token-aware factories ===\n";

    using Factory = std::function<net::awaitable<void>(std::stop_token)>;
    std::vector<Factory> factories;
    factories.push_back(task_void_factory(1, 6s));
    factories.push_back(task_void_factory(2, 2s));
    factories.push_back(task_void_factory(3, 4s));
    factories.push_back(task_void_factory(4, 1s));

    auto winner_index = co_await NSNAME::when_any<void>(std::move(factories));

    std::cout << "Winner: Task " << winner_index << "\n";

    // Give time to see if cancelled tasks print (they shouldn't)
    auto timer = net::steady_timer(co_await net::this_coro::executor);
    timer.expires_after(7s);
    co_await timer.async_wait(net::use_awaitable);

    std::cout
        << "=== Waited 7s after winner - cancelled tasks should NOT have printed ===\n";
}

net::awaitable<void> example_value_vector_with_cancellation()
{
    std::cout
        << "\n=== Example 2: when_any with value stop-token-aware factories ===\n";

    using Factory = std::function<net::awaitable<int>(std::stop_token)>;
    std::vector<Factory> factories;
    factories.push_back(task_value_factory(1, 5s));
    factories.push_back(task_value_factory(2, 1s));
    factories.push_back(task_value_factory(3, 3s));

    auto [winner_index,
          result] = co_await NSNAME::when_any<int>(std::move(factories));

    std::cout << "Winner: Task " << winner_index << " with value " << result
              << "\n";

    // Give time to see if cancelled tasks print (they shouldn't)
    auto timer = net::steady_timer(co_await net::this_coro::executor);
    timer.expires_after(6s);
    co_await timer.async_wait(net::use_awaitable);

    std::cout
        << "=== Waited 6s after winner - cancelled tasks should NOT have printed ===\n";
}

net::awaitable<void> example_variadic_with_cancellation()
{
    std::cout
        << "\n=== Example 3: when_any with variadic factory arguments ===\n";

    auto [winner_index, result] = co_await NSNAME::when_any(
        task_value_factory(1, 4s), task_value_factory(2, 2s),
        task_value_factory(3, 6s));

    std::cout << "Winner: Task " << winner_index << " with value ";
    std::visit([](auto&& v) { std::cout << v; }, result);
    std::cout << "\n";

    // Give time to see if cancelled tasks print (they shouldn't)
    auto timer = net::steady_timer(co_await net::this_coro::executor);
    timer.expires_after(7s);
    co_await timer.async_wait(net::use_awaitable);

    std::cout
        << "=== Waited 7s after winner - cancelled tasks should NOT have printed ===\n";
}

net::awaitable<void> run_examples()
{
    try
    {
        co_await example_void_vector_with_cancellation();
        co_await example_value_vector_with_cancellation();
        co_await example_variadic_with_cancellation();

        std::cout << "\n=== All examples completed successfully ===\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
    }
}

int main()
{
    try
    {
        net::io_context ioc;

        net::co_spawn(ioc, run_examples(), net::detached);

        ioc.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
