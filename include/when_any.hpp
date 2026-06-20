#pragma once
#include "beastdefs.hpp"
#include "logger.hpp"

#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/experimental/parallel_group.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <stop_token>
#include <tuple>
#include <utility>
#include <variant>

namespace NSNAME
{

namespace detail
{
// Race barrier for coordinating first completion of multiple concurrent tasks
class race_barrier
{
  public:
    explicit race_barrier(const net::any_io_executor& exec, std::size_t count) :
        remaining_(count), completed_(false),
        timer_(std::make_shared<net::steady_timer>(exec)),
        stop_source_(std::make_shared<std::stop_source>())
    {
        timer_->expires_at(std::chrono::steady_clock::time_point::max());
    }

    // Get completion handler for co_spawn
    auto completion_handler()
    {
        return [this](std::exception_ptr) {
            bool expected = false;
            if (completed_.compare_exchange_strong(expected, true))
            {
                // First task to complete - signal others to stop
                stop_source_->request_stop();
            }

            // Decrement remaining count
            if (--remaining_ == 0)
            {
                // All tasks finished - cancel the wait timer
                timer_->cancel();
            }
        };
    }

    // Wait for first task to complete
    net::awaitable<void> wait()
    {
        boost::system::error_code ec;
        co_await timer_->async_wait(
            net::redirect_error(net::use_awaitable, ec));
    }

    // Get stop token for tasks
    std::stop_token get_stop_token()
    {
        return stop_source_->get_token();
    }

    bool is_completed() const
    {
        return completed_.load();
    }

  private:
    std::atomic<std::size_t> remaining_;
    std::atomic<bool> completed_;
    std::shared_ptr<net::steady_timer> timer_;
    std::shared_ptr<std::stop_source> stop_source_;
};

template <std::size_t I, std::size_t N, typename Factory>
void launch_any_void_factory(const net::any_io_executor& exec, Factory factory,
                             race_barrier& barrier,
                             std::array<std::exception_ptr, N>& errors,
                             std::atomic<std::size_t>& completed_index)
{
    net::co_spawn(
        exec,
        [&errors, &completed_index, &barrier,
         factory = std::move(factory)]() mutable -> net::awaitable<void> {
            co_await net::post(co_await net::this_coro::executor,
                               net::use_awaitable);
            try
            {
                auto stop_token = barrier.get_stop_token();
                auto task = factory(stop_token);
                co_await std::move(task);

                // Mark this task as the winner if not already completed
                std::size_t expected = N;
                completed_index.compare_exchange_strong(expected, I);
            }
            catch (...)
            {
                errors[I] = std::current_exception();
            }
        },
        barrier.completion_handler());
}

template <std::size_t I, typename ValueTuple, std::size_t N, typename Factory>
void launch_any_value_factory(const net::any_io_executor& exec, Factory factory,
                              race_barrier& barrier, ValueTuple& values,
                              std::array<std::exception_ptr, N>& errors,
                              std::atomic<std::size_t>& completed_index)
{
    net::co_spawn(
        exec,
        [&values, &errors, &completed_index, &barrier,
         factory = std::move(factory)]() mutable -> net::awaitable<void> {
            co_await net::post(co_await net::this_coro::executor,
                               net::use_awaitable);
            try
            {
                auto stop_token = barrier.get_stop_token();
                auto task = factory(stop_token);
                std::get<I>(values) = co_await std::move(task);

                // Mark this task as the winner if not already completed
                std::size_t expected = N;
                completed_index.compare_exchange_strong(expected, I);
            }
            catch (...)
            {
                errors[I] = std::current_exception();
            }
        },
        barrier.completion_handler());
}

template <typename... Factories>
net::awaitable<std::size_t> when_any_void_impl(const net::any_io_executor& exec,
                                               Factories... factories)
{
    constexpr std::size_t numTasks = sizeof...(Factories);
    std::array<std::exception_ptr, numTasks> errors{};
    std::atomic<std::size_t> completed_index{numTasks};
    race_barrier barrier(exec, numTasks);

    auto launch = [&]<std::size_t I>(std::integral_constant<std::size_t, I>,
                                     auto factory) {
        launch_any_void_factory<I, numTasks>(exec, std::move(factory), barrier,
                                             errors, completed_index);
    };

    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (launch(std::integral_constant<std::size_t, I>{}, std::move(factories)),
         ...);
    }(std::index_sequence_for<Factories...>{});

    co_await barrier.wait();

    // Check if the completed task had an error
    std::size_t winner = completed_index.load();
    if (winner < numTasks && errors[winner])
    {
        LOG_DEBUG("when_any: Winner task {} failed. Rethrowing exception.",
                  winner);
        std::rethrow_exception(errors[winner]);
    }

    co_return winner;
}

template <typename... Factories>
    requires(sizeof...(Factories) > 0)
net::awaitable<
    std::pair<std::size_t, std::variant<typename std::invoke_result_t<
                               Factories, std::stop_token>::value_type...>>>
    when_any_value_impl(const net::any_io_executor& exec,
                        Factories... factories)
{
    std::tuple<std::optional<typename std::invoke_result_t<
        Factories, std::stop_token>::value_type>...>
        values;
    std::array<std::exception_ptr, sizeof...(Factories)> errors{};
    std::atomic<std::size_t> completed_index{sizeof...(Factories)};
    race_barrier barrier(exec, sizeof...(Factories));

    auto launch = [&]<std::size_t I>(std::integral_constant<std::size_t, I>,
                                     auto factory) {
        launch_any_value_factory<I, decltype(values), sizeof...(Factories)>(
            exec, std::move(factory), barrier, values, errors, completed_index);
    };

    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (launch(std::integral_constant<std::size_t, I>{}, std::move(factories)),
         ...);
    }(std::index_sequence_for<Factories...>{});

    co_await barrier.wait();

    std::size_t winner = completed_index.load();

    // Check if the completed task had an error
    if (winner < sizeof...(Factories) && errors[winner])
    {
        LOG_DEBUG("when_any: Winner task {} failed. Rethrowing exception.",
                  winner);
        std::rethrow_exception(errors[winner]);
    }

    // Check if winner has a value
    bool has_value = false;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((I == winner && std::get<I>(values).has_value()
              ? (has_value = true, true)
              : false) ||
         ...);
    }(std::index_sequence_for<Factories...>{});

    if (!has_value)
    {
        throw std::runtime_error("Winner task did not produce a value");
    }

    // Extract the winning value into a variant
    auto extract_value = [&]<std::size_t... I>(std::index_sequence<I...>)
        -> std::variant<typename std::invoke_result_t<
            Factories, std::stop_token>::value_type...> {
        std::variant<typename std::invoke_result_t<
            Factories, std::stop_token>::value_type...>
            result;
        ((I == winner && std::get<I>(values).has_value()
              ? (result.template emplace<I>(std::move(*std::get<I>(values))),
                 true)
              : false) ||
         ...);
        return result;
    };

    co_return std::make_pair(
        winner, extract_value(std::index_sequence_for<Factories...>{}));
}
} // namespace detail

// when_any for variadic factory functions (tuple-based, stop_token aware)
template <typename... Factories>
    requires(sizeof...(Factories) > 0) &&
            (std::invocable<Factories, std::stop_token> && ...)
auto when_any(Factories... factories) -> std::conditional_t<
    (std::is_void_v<typename std::invoke_result_t<
         Factories, std::stop_token>::value_type> &&
     ...),
    net::awaitable<std::size_t>,
    net::awaitable<std::pair<std::size_t,
                             std::variant<typename std::invoke_result_t<
                                 Factories, std::stop_token>::value_type...>>>>
{
    using FirstFactory = std::tuple_element_t<0, std::tuple<Factories...>>;
    using FirstResult = std::invoke_result_t<FirstFactory, std::stop_token>;
    using ValueType = typename FirstResult::value_type;

    static_assert(
        (std::is_void_v<ValueType> &&
         (std::is_void_v<typename std::invoke_result_t<
              Factories, std::stop_token>::value_type> &&
          ...)) ||
            (!std::is_void_v<ValueType> &&
             (!std::is_void_v<typename std::invoke_result_t<
                  Factories, std::stop_token>::value_type> &&
              ...)),
        "when_any tuple overload supports either all-void or all-non-void factories");

    auto exec = co_await net::this_coro::executor;

    LOG_DEBUG("when_any: Launching {} tuple-based factory tasks concurrently",
              sizeof...(Factories));

    if constexpr (std::is_void_v<ValueType>)
    {
        co_return co_await detail::when_any_void_impl(exec,
                                                      std::move(factories)...);
    }
    else
    {
        co_return co_await detail::when_any_value_impl(exec,
                                                       std::move(factories)...);
    }
}

// when_any for vector of awaitable factories (stop_token based)
template <typename T, typename Factory>
    requires std::invocable<Factory, std::stop_token> &&
             std::same_as<std::invoke_result_t<Factory, std::stop_token>,
                          net::awaitable<T>>
auto when_any(std::vector<Factory> factories)
    -> net::awaitable<std::conditional_t<std::is_void_v<T>, std::size_t,
                                         std::pair<std::size_t, T>>>
{
    if (factories.empty())
    {
        throw std::invalid_argument("when_any requires at least one factory");
    }

    auto exec = co_await net::this_coro::executor;
    size_t num_tasks = factories.size();

    LOG_DEBUG("when_any: Launching {} vector tasks concurrently", num_tasks);

    std::vector<std::exception_ptr> errors(num_tasks);
    std::atomic<std::size_t> completed_index{num_tasks};
    detail::race_barrier barrier(exec, num_tasks);

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
            [&values, &errors, &completed_index, &barrier, i, num_tasks,
             factory =
                 std::move(factories[i])]() mutable -> net::awaitable<void> {
                co_await net::post(co_await net::this_coro::executor,
                                   net::use_awaitable);
                try
                {
                    // Create task with stop token
                    auto stop_token = barrier.get_stop_token();
                    auto task = factory(stop_token);

                    if constexpr (std::is_void_v<T>)
                    {
                        co_await std::move(task);
                    }
                    else
                    {
                        values[i] = co_await std::move(task);
                    }

                    // Mark this task as the winner - stop is requested in
                    // completion_handler
                    std::size_t expected = num_tasks;
                    completed_index.compare_exchange_strong(expected, i);
                }
                catch (...)
                {
                    errors[i] = std::current_exception();
                }
            },
            barrier.completion_handler());
    }

    // Wait for first task to complete
    co_await barrier.wait();

    std::size_t winner = completed_index.load();

    // Check if the completed task had an error
    if (winner < num_tasks && errors[winner])
    {
        LOG_DEBUG("when_any: Winner task {} failed. Rethrowing exception.",
                  winner);
        std::rethrow_exception(errors[winner]);
    }

    if constexpr (std::is_void_v<T>)
    {
        co_return winner;
    }
    else
    {
        // Check if winner has a value
        if (winner < num_tasks && values[winner].has_value())
        {
            co_return std::make_pair(winner, std::move(*values[winner]));
        }
        else
        {
            throw std::runtime_error("Winner task did not produce a value");
        }
    }
}

// when_any for vector of awaitables (legacy, no cancellation support)
template <typename T>
auto when_any(std::vector<net::awaitable<T>> awaitables)
    -> net::awaitable<std::conditional_t<std::is_void_v<T>, std::size_t,
                                         std::pair<std::size_t, T>>>
{
    if (awaitables.empty())
    {
        throw std::invalid_argument("when_any requires at least one awaitable");
    }

    auto exec = co_await net::this_coro::executor;
    size_t num_tasks = awaitables.size();

    LOG_DEBUG(
        "when_any: Launching {} vector tasks concurrently (no cancellation)",
        num_tasks);

    std::vector<std::exception_ptr> errors(num_tasks);
    std::atomic<std::size_t> completed_index{num_tasks};
    detail::race_barrier barrier(exec, num_tasks);

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
            [&values, &errors, &completed_index, &barrier, i, num_tasks,
             task =
                 std::move(awaitables[i])]() mutable -> net::awaitable<void> {
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

                    // Mark this task as the winner if not already completed
                    std::size_t expected = num_tasks;
                    completed_index.compare_exchange_strong(expected, i);
                }
                catch (const boost::system::system_error& e)
                {
                    // Ignore cancellation errors
                    if (e.code() != net::error::operation_aborted)
                    {
                        errors[i] = std::current_exception();
                    }
                }
                catch (...)
                {
                    errors[i] = std::current_exception();
                }
            },
            barrier.completion_handler());
    }

    // Wait for first task to complete
    co_await barrier.wait();

    std::size_t winner = completed_index.load();

    // Check if the completed task had an error
    if (winner < num_tasks && errors[winner])
    {
        LOG_DEBUG("when_any: Winner task {} failed. Rethrowing exception.",
                  winner);
        std::rethrow_exception(errors[winner]);
    }

    if constexpr (std::is_void_v<T>)
    {
        co_return winner;
    }
    else
    {
        co_return std::make_pair(winner, std::move(*values[winner]));
    }
}

} // namespace NSNAME
