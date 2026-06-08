/**
 * @file completion_handler.hpp
 * @brief Generic completion handler utilities for coroutines
 *
 * Provides reusable completion handler functions that handle exceptions
 * and execute continuation callbacks.
 */

#pragma once

#include "logger.hpp"

#include <exception>
#include <functional>
#include <string>

namespace reactor
{

/**
 * @brief Log exception from exception_ptr
 * @param ex Exception pointer to log
 * @param context Context string to include in log message
 */
inline void logException(std::exception_ptr ex, const std::string& context)
{
    if (ex)
    {
        try
        {
            std::rethrow_exception(ex);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("{}: {}", context, e.what());
        }
    }
}

/**
 * @brief Create a generic completion handler for coroutines
 *
 * This function creates a completion handler that:
 * 1. Logs any exception that occurred during coroutine execution
 * 2. Executes a continuation callback regardless of success or failure
 *
 * @tparam Callback Type of the continuation callback
 * @param context Context string for exception logging
 * @param continuation Callback to execute after handling exception
 * @return Completion handler function
 *
 * @example
 * // Simple usage with lambda
 * auto handler = makeCompletionHandler("MyTask", [&]() {
 *     ioContext.stop();
 * });
 *
 * @example
 * // Usage with captured variables
 * auto handler = makeCompletionHandler("Console " + name,
 *     [this, name]() {
 *         removeInstance(name);
 *     });
 */
template <typename Callback>
inline auto makeCompletionHandler(const std::string& context,
                                  Callback&& continuation)
{
    return [context, continuation = std::forward<Callback>(continuation)](
               std::exception_ptr ex) {
        logException(ex, context);
        continuation();
    };
}

/**
 * @brief Create a completion handler with exception-aware continuation
 *
 * This variant passes the exception_ptr to the continuation callback,
 * allowing the callback to handle exceptions differently based on
 * whether an exception occurred.
 *
 * @tparam Callback Type of the continuation callback (must accept
 * exception_ptr)
 * @param context Context string for exception logging
 * @param continuation Callback that receives exception_ptr
 * @return Completion handler function
 *
 * @example
 * auto handler = makeCompletionHandlerWithException("MyTask",
 *     [&](std::exception_ptr ex) {
 *         if (ex) {
 *             // Handle error case
 *             cleanup();
 *         }
 *         ioContext.stop();
 *     });
 */
template <typename Callback>
inline auto makeCompletionHandlerWithException(const std::string& context,
                                               Callback&& continuation)
{
    return [context, continuation = std::forward<Callback>(continuation)](
               std::exception_ptr ex) {
        logException(ex, context);
        continuation(ex);
    };
}

} // namespace reactor
