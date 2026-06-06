#pragma once
#include "beastdefs.hpp"

#include <boost/asio/steady_timer.hpp>

#include <chrono>

namespace NSNAME
{

/**
 * @brief Generic timer-based wait utilities for coroutines
 *
 * Provides convenient async wait functions for use in Boost.Asio coroutines.
 * Two versions are available:
 * 1. Implicit timer creation - creates and manages timer internally
 * 2. Explicit timer reference - uses an existing timer for waiting
 *
 * All functions return error_code for consistent error handling.
 */

/**
 * @brief Wait for a duration with implicit timer creation
 *
 * Creates a steady_timer internally and waits for the specified duration.
 * This is the simplest form - just specify executor and duration.
 *
 * @param executor The executor to run the timer on
 * @param duration The duration to wait (any std::chrono duration type)
 * @return Awaitable that returns error_code (empty on success, set on
 * error/cancel)
 *
 * @example
 * auto ec = co_await waitFor(executor_, std::chrono::milliseconds(10));
 * if (ec) {
 *     // Handle error or cancellation
 * }
 *
 * // Or ignore the error if not needed
 * co_await waitFor(executor_, std::chrono::seconds(1));
 */
inline net::awaitable<boost::system::error_code> waitFor(
    net::any_io_executor executor, net::steady_timer::duration duration)
{
    net::steady_timer timer(executor);
    timer.expires_after(duration);
    auto [ec] =
        co_await timer.async_wait(boost::asio::as_tuple(net::use_awaitable));
    co_return ec;
}

/**
 * @brief Wait for a duration using an existing timer reference
 *
 * Uses an existing steady_timer for waiting. This is useful when you want
 * to reuse a timer object or need more control over timer lifecycle.
 *
 * @param timer Reference to an existing steady_timer
 * @param duration The duration to wait (any std::chrono duration type)
 * @return Awaitable that returns error_code (empty on success, set on
 * error/cancel)
 *
 * @example
 * net::steady_timer timer(executor_);
 * auto ec = co_await waitFor(timer, std::chrono::milliseconds(10));
 * if (ec == boost::asio::error::operation_aborted) {
 *     // Timer was cancelled
 * }
 *
 * // Reuse the same timer
 * co_await waitFor(timer, std::chrono::milliseconds(20));
 */
inline net::awaitable<boost::system::error_code> waitFor(
    net::steady_timer& timer, net::steady_timer::duration duration)
{
    timer.expires_after(duration);
    auto [ec] =
        co_await timer.async_wait(boost::asio::as_tuple(net::use_awaitable));
    co_return ec;
}

} // namespace NSNAME
