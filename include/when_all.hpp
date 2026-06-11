#pragma once
#include "beastdefs.hpp"
#include "logger.hpp"

#include <boost/asio/experimental/parallel_group.hpp>

#include <array>
#include <tuple>
#include <utility>
#include <variant>

namespace NSNAME
{

namespace detail
{
// Task barrier for coordinating completion of multiple concurrent tasks
class task_barrier
{
  public:
    explicit task_barrier(const net::any_io_executor& exec, std::size_t count) :
        remaining_(count), timer_(std::make_shared<net::steady_timer>(exec))
    {
        timer_->expires_at(std::chrono::steady_clock::time_point::max());
    }

    // Get completion handler for co_spawn
    auto completion_handler()
    {
        return [this](std::exception_ptr) {
            if (--remaining_ == 0)
            {
                timer_->cancel();
            }
        };
    }

    // Wait for all tasks to complete
    net::awaitable<void> wait()
    {
        boost::system::error_code ec;
        co_await timer_->async_wait(
            net::redirect_error(net::use_awaitable, ec));
    }

  private:
    std::size_t remaining_;
    std::shared_ptr<net::steady_timer> timer_;
};

template <std::size_t I, std::size_t N>
void launch_void_task(const net::any_io_executor& exec, auto aw,
                      task_barrier& barrier,
                      std::array<std::exception_ptr, N>& errors)
{
    net::co_spawn(
        exec,
        [&errors, task = std::move(aw)]() mutable -> net::awaitable<void> {
            co_await net::post(co_await net::this_coro::executor,
                               net::use_awaitable);
            try
            {
                co_await std::move(task);
            }
            catch (...)
            {
                errors[I] = std::current_exception();
            }
        },
        barrier.completion_handler());
}

template <std::size_t I, typename ValueTuple, std::size_t N>
void launch_value_task(const net::any_io_executor& exec, auto aw,
                       task_barrier& barrier, ValueTuple& values,
                       std::array<std::exception_ptr, N>& errors)
{
    net::co_spawn(
        exec,
        [&values, &errors,
         task = std::move(aw)]() mutable -> net::awaitable<void> {
            co_await net::post(co_await net::this_coro::executor,
                               net::use_awaitable);
            try
            {
                std::get<I>(values) = co_await std::move(task);
            }
            catch (...)
            {
                errors[I] = std::current_exception();
            }
        },
        barrier.completion_handler());
}
template <typename ErrorContainer>
void check_and_rethrow_errors(const ErrorContainer& errors,
                              const char* context = "when_all")
{
    for (const auto& err_ptr : errors)
    {
        if (err_ptr)
        {
            LOG_DEBUG("{}: A sub-task failed. Rethrowing exception.", context);
            std::rethrow_exception(err_ptr);
        }
    }
}
template <typename... Awaitables>
net::awaitable<void> when_all_void_impl(const net::any_io_executor& exec,
                                        Awaitables... awaitables)
{
    constexpr std::size_t numTasks = sizeof...(Awaitables);
    std::array<std::exception_ptr, numTasks> errors{};
    task_barrier barrier(exec, numTasks);

    auto launch =
        [&]<std::size_t I>(std::integral_constant<std::size_t, I>, auto aw) {
            launch_void_task<I>(exec, std::move(aw), barrier, errors);
        };

    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (launch(std::integral_constant<std::size_t, I>{},
                std::move(awaitables)),
         ...);
    }(std::index_sequence_for<Awaitables...>{});

    co_await barrier.wait();

    check_and_rethrow_errors(errors);
}

template <typename... Awaitables>
net::awaitable<std::tuple<typename Awaitables::value_type...>>
    when_all_value_impl(const net::any_io_executor& exec,
                        Awaitables... awaitables)
{
    std::tuple<std::optional<typename Awaitables::value_type>...> values;
    std::array<std::exception_ptr, sizeof...(Awaitables)> errors{};
    task_barrier barrier(exec, sizeof...(Awaitables));

    auto launch =
        [&]<std::size_t I>(std::integral_constant<std::size_t, I>, auto aw) {
            launch_value_task<I>(exec, std::move(aw), barrier, values, errors);
        };

    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (launch(std::integral_constant<std::size_t, I>{},
                std::move(awaitables)),
         ...);
    }(std::index_sequence_for<Awaitables...>{});

    co_await barrier.wait();

    check_and_rethrow_errors(errors);

    co_return [&]<std::size_t... I>(std::index_sequence<I...>) {
        return std::tuple<typename Awaitables::value_type...>{
            std::move(*std::get<I>(values))...};
    }(std::index_sequence_for<Awaitables...>{});
}
} // namespace detail

template <typename... Awaitables>
using when_all_tuple_result_t =
    std::conditional_t<(std::is_void_v<typename Awaitables::value_type> && ...),
                       void, std::tuple<typename Awaitables::value_type...>>;

template <typename... Awaitables>
net::awaitable<when_all_tuple_result_t<Awaitables...>> when_all(
    Awaitables... awaitables)
{
    static_assert(
        ((std::is_void_v<typename Awaitables::value_type>) && ...) ||
            ((!std::is_void_v<typename Awaitables::value_type>) && ...),
        "when_all tuple overload supports either all-void or all-non-void awaitables");

    auto exec = co_await net::this_coro::executor;

    LOG_DEBUG("when_all: Launching {} tuple-based tasks concurrently",
              sizeof...(Awaitables));

    if constexpr ((std::is_void_v<typename Awaitables::value_type> && ...))
    {
        co_await detail::when_all_void_impl(exec, std::move(awaitables)...);
    }
    else
    {
        co_return co_await detail::when_all_value_impl(
            exec, std::move(awaitables)...);
    }
}
template <typename T>
auto when_all(std::vector<net::awaitable<T>> awaitables) -> net::awaitable<
    std::conditional_t<std::is_void_v<T>, void, std::vector<T>>>
{
    if (awaitables.empty())
    {
        if constexpr (std::is_void_v<T>)
        {
            co_return;
        }
        else
        {
            co_return std::vector<T>{};
        }
    }

    auto exec = co_await net::this_coro::executor;
    size_t num_tasks = awaitables.size();

    LOG_DEBUG("when_all: Launching {} vector tasks concurrently", num_tasks);

    std::vector<std::exception_ptr> errors(num_tasks);
    detail::task_barrier barrier(exec, num_tasks);

    // Storage for non-void results
    std::conditional_t<std::is_void_v<T>, std::monostate,
                       std::vector<std::optional<T>>>
        values;
    if constexpr (!std::is_void_v<T>)
    {
        values.resize(num_tasks);
    }

    // Launch all tasks concurrently
    for (size_t i = 0; i < num_tasks; ++i)
    {
        net::co_spawn(
            exec,
            [&values, &errors, i, task = std::move(awaitables[i])]() mutable
                -> net::awaitable<void> {
                co_await net::post(co_await net::this_coro::executor,
                                   net::use_awaitable);
                try
                {
                    if constexpr (std::is_void_v<T>)
                    {
                        co_await std::move(task);
                    }
                    else
                    {
                        values[i] = co_await std::move(task);
                    }
                }
                catch (...)
                {
                    errors[i] = std::current_exception();
                }
            },
            barrier.completion_handler());
    }

    // Wait for all tasks to complete
    co_await barrier.wait();

    detail::check_and_rethrow_errors(errors, "when_all: Dynamic task array");

    if constexpr (!std::is_void_v<T>)
    {
        std::vector<T> result;
        result.reserve(num_tasks);
        for (auto& value : values)
        {
            result.push_back(std::move(*value));
        }
        co_return result;
    }
}

} // namespace NSNAME
