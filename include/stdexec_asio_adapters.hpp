#pragma once

#include "beastdefs.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <concepts>
#include <exception>
#include <optional>
#include <utility>

// Note: This requires stdexec library (P2300 reference implementation)
// Install: https://github.com/NVIDIA/stdexec

#ifdef __has_include
#if __has_include(<stdexec/execution.hpp>)
#include <stdexec/execution.hpp>
#define HAS_STDEXEC 1
#else
#define HAS_STDEXEC 0
#endif
#else
#define HAS_STDEXEC 0
#endif

namespace NSNAME
{

#if HAS_STDEXEC

namespace stdexec_adapters
{

// ============================================================================
// Asio Scheduler for stdexec (spawns awaitables)
// ============================================================================

/**
 * @brief A stdexec scheduler that spawns Boost.Asio awaitables
 *
 * This scheduler uses co_spawn to execute awaitables, making it suitable
 * for converting awaitables into senders that need to be scheduled.
 * Use this when you need to bridge from awaitable to sender world.
 *
 * @tparam T The value type of the awaitable
 * @tparam Executor The executor type
 */
template <typename T, typename Executor = net::any_io_executor>
class AsioScheduler
{
  public:
    using sender_concept = stdexec::sender_t;

    explicit AsioScheduler(net::awaitable<T, Executor> aw, Executor ex) :
        awaitable_(std::move(aw)), executor_(ex)
    {}

    template <typename Receiver>
    struct operation_state
    {
        net::awaitable<T, Executor> awaitable;
        Executor executor;
        Receiver receiver;

        friend void tag_invoke(stdexec::start_t, operation_state& self) noexcept
        {
            // Spawn the awaitable with a completion handler that forwards to
            // receiver
            if constexpr (std::is_void_v<T>)
            {
                net::co_spawn(
                    self.executor,
                    [aw = std::move(
                         self.awaitable)]() mutable -> net::awaitable<void> {
                        co_await std::move(aw);
                        co_return;
                    }(),
                    [rcv = std::move(self.receiver)](
                        std::exception_ptr ex) mutable {
                        if (ex)
                        {
                            stdexec::set_error(std::move(rcv), ex);
                        }
                        else
                        {
                            stdexec::set_value(std::move(rcv));
                        }
                    });
            }
            else
            {
                net::co_spawn(
                    self.executor,
                    [aw = std::move(
                         self.awaitable)]() mutable -> net::awaitable<T> {
                        T result = co_await std::move(aw);
                        co_return result;
                    }(),
                    [rcv = std::move(self.receiver)](std::exception_ptr ex,
                                                     T result) mutable {
                        if (ex)
                        {
                            stdexec::set_error(std::move(rcv), ex);
                        }
                        else
                        {
                            stdexec::set_value(std::move(rcv),
                                               std::move(result));
                        }
                    });
            }
        }
    };

    template <typename Receiver>
    friend auto tag_invoke(stdexec::connect_t, AsioScheduler&& self,
                           Receiver&& rcv)
    {
        return operation_state<std::remove_cvref_t<Receiver>>{
            std::move(self.awaitable_), self.executor_,
            std::forward<Receiver>(rcv)};
    }

    template <typename Env>
    friend auto tag_invoke(stdexec::get_completion_signatures_t,
                           AsioScheduler&&, Env&&)
    {
        if constexpr (std::is_void_v<T>)
        {
            return stdexec::completion_signatures<
                stdexec::set_value_t(),
                stdexec::set_error_t(std::exception_ptr)>{};
        }
        else
        {
            return stdexec::completion_signatures<
                stdexec::set_value_t(T),
                stdexec::set_error_t(std::exception_ptr)>{};
        }
    }

  private:
    net::awaitable<T, Executor> awaitable_;
    Executor executor_;
};

/**
 * @brief Helper function to create an AsioScheduler (for spawning awaitables)
 */
template <typename T>
auto to_sender(net::awaitable<T> aw, net::any_io_executor ex)
{
    return AsioScheduler<T, net::any_io_executor>(std::move(aw), ex);
}

// ============================================================================
// stdexec Sender → Asio Awaitable Adapter
// ============================================================================

/**
 * @brief Receiver that bridges stdexec sender completion to stored result
 * and cancels a timer when complete
 */
template <typename T>
struct awaitable_receiver
{
    using receiver_concept = stdexec::receiver_t;

    std::optional<T>* result;
    std::exception_ptr* error;
    net::steady_timer* timer;

    // Implement set_value for the receiver concept
    template <typename... Values>
    friend void tag_invoke(stdexec::set_value_t, awaitable_receiver&& self,
                           Values&&... values) noexcept
    {
        try
        {
            if constexpr (sizeof...(Values) == 0)
            {
                self.result->emplace(T{});
            }
            else if constexpr (sizeof...(Values) == 1)
            {
                self.result->emplace(std::forward<Values>(values)...);
            }
            else
            {
                self.result->emplace(
                    std::make_tuple(std::forward<Values>(values)...));
            }
        }
        catch (...)
        {
            *self.error = std::current_exception();
        }
        // Cancel the timer to wake up the awaitable
        self.timer->cancel();
    }

    // Implement set_error for the receiver concept
    template <typename Error>
    friend void tag_invoke(stdexec::set_error_t, awaitable_receiver&& self,
                           Error&& err) noexcept
    {
        if constexpr (std::is_same_v<std::decay_t<Error>, std::exception_ptr>)
        {
            *self.error = std::forward<Error>(err);
        }
        else
        {
            *self.error = std::make_exception_ptr(std::forward<Error>(err));
        }
        // Cancel the timer to wake up the awaitable
        self.timer->cancel();
    }

    // Implement set_stopped for the receiver concept
    friend void tag_invoke(stdexec::set_stopped_t,
                           awaitable_receiver&& self) noexcept
    {
        *self.error =
            std::make_exception_ptr(std::runtime_error("Operation stopped"));
        // Cancel the timer to wake up the awaitable
        self.timer->cancel();
    }

    // Provide get_env for the receiver
    friend stdexec::env<> tag_invoke(stdexec::get_env_t,
                                     const awaitable_receiver&) noexcept
    {
        return {};
    }
};

// ============================================================================
// Type Unwrapping Utilities
// ============================================================================

/**
 * @brief Unwraps variant<tuple<T>> to just T for single-value senders
 */
template <typename T>
struct unwrap_sender_value_type
{
    using type = T;
};

template <typename T>
struct unwrap_sender_value_type<std::variant<std::tuple<T>>>
{
    using type = T;
};

template <typename T>
using unwrap_sender_value_type_t = typename unwrap_sender_value_type<T>::type;

/**
 * @brief Converts a stdexec sender into an awaitable
 *
 * This creates an awaitable that executes the sender and can be co_awaited.
 * Uses an infinite timer that gets canceled when the operation completes,
 * avoiding busy loops.
 *
 * For senders with a single value type, automatically unwraps from
 * variant<tuple<T>> to just T for cleaner usage (similar to sync_wait).
 *
 * @tparam Sender The sender type
 * @param sender The sender to execute
 * @param ex The executor for async operations
 * @return An awaitable that completes when the sender completes
 */
template <stdexec::sender Sender>
auto as_awaitable(Sender&& sender, net::any_io_executor ex)
{
    // Get the raw value type from the sender (variant<tuple<...>>)
    using raw_value_type =
        typename stdexec::value_types_of_t<Sender, stdexec::env<>, std::tuple,
                                           std::variant>;

    // Unwrap single-value senders: variant<tuple<T>> -> T
    using value_type = unwrap_sender_value_type_t<raw_value_type>;

    // Return an awaitable that executes the sender
    return [](Sender sender,
              net::any_io_executor ex) -> net::awaitable<value_type> {
        // Storage for the result or error (use raw type for receiver)
        std::optional<raw_value_type> result;
        std::exception_ptr error;

        // Create an infinite timer that will be canceled when the operation
        // completes
        net::steady_timer timer(ex);
        timer.expires_at(net::steady_timer::time_point::max());

        // Connect the sender with our receiver
        auto op = stdexec::connect(
            std::move(sender),
            awaitable_receiver<raw_value_type>{&result, &error, &timer});

        // Start the operation
        stdexec::start(op);

        // Wait on the infinite timer - it will be canceled when the operation
        // completes. This avoids the busy loop by suspending until canceled.
        // We expect operation_aborted when the timer is canceled.
        boost::system::error_code ec;
        co_await timer.async_wait(net::redirect_error(net::use_awaitable, ec));

        // Check for errors from the sender operation
        if (error)
        {
            std::rethrow_exception(error);
        }

        // Unwrap and return the result
        if constexpr (std::is_same_v<value_type, raw_value_type>)
        {
            // Multi-value sender: return as-is
            co_return std::move(*result);
        }
        else
        {
            // Single-value sender: unwrap variant<tuple<T>> -> T
            co_return std::get<0>(std::get<0>(*result));
        }
    }(std::forward<Sender>(sender), ex);
}

} // namespace stdexec_adapters

#endif // HAS_STDEXEC

} // namespace NSNAME
