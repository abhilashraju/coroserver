#pragma once
/**
 * Async port of libspdm_get_version() / libspdm_try_get_version().
 *
 * Replaces the two blocking I/O call sites inside the upstream C function
 * (libspdm_send_spdm_request / libspdm_receive_spdm_response via
 * context->send_message / context->receive_message) with co_await calls to
 * libspdm_send_request_async / libspdm_receive_response_async.
 *
 * All CPU-bound logic (request construction, response validation, version
 * negotiation, transcript update, connection-state mutation) is preserved
 * verbatim from:
 *   library/spdm_requester_lib/libspdm_req_get_version.c
 *
 * Retry semantics for LIBSPDM_STATUS_BUSY_PEER match the upstream outer
 * wrapper, but use co_await steady_timer instead of libspdm_sleep().
 */

#include "libspdm_async_io.hpp"
#include "libspdm_async_utils.hpp"
#include "libspdm_req_send_receive_async.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/this_coro.hpp>

#include <chrono>

extern "C"
{
#include <internal/libspdm_common_lib.h>
#include <internal/libspdm_requester_lib.h>
#include <library/spdm_common_lib.h>
}

namespace detail
{

#pragma pack(1)
struct libspdm_version_response_max_async_t
{
    spdm_message_header_t header;
    uint8_t reserved;
    uint8_t version_number_entry_count;
    spdm_version_number_t version_number_entry[LIBSPDM_MAX_VERSION_COUNT];
};
#pragma pack()

/**
 * Coroutine port of libspdm_try_get_version().
 * Sends GET_VERSION and receives VERSION, updating spdm_context in place.
 *
 * Improvements vs. the original C port:
 *  - goto replaced by ScopeExit RAII guard on the receiver buffer.
 *  - Variables declared at point of first use.
 *  - Redundant split version_number_entry_count checks collapsed into one.
 *  - as_void_pp() removes verbose reinterpret_cast<void**> at call sites.
 *  - is_requester set before connection_state (logical ordering).
 */
template <AsyncSpdmIO IO>
boost::asio::awaitable<libspdm_return_t>
    libspdm_try_get_version_async(libspdm_context_t* context, IO& io)
{
    /* -=[Set State Phase]=- */
    libspdm_reset_context(context);

    // -----------------------------------------------------------------------
    // Construct Request Phase
    // -----------------------------------------------------------------------
    const size_t transport_header_size =
        context->local_context.capability.transport_header_size;

    uint8_t* message     = nullptr;
    size_t   message_size = 0;
    libspdm_return_t status =
        libspdm_acquire_sender_buffer(context, &message_size,
                                      as_void_pp(&message));
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    LIBSPDM_ASSERT(message_size >= transport_header_size +
                                       context->local_context.capability
                                           .transport_tail_size);

    auto* spdm_request = reinterpret_cast<spdm_get_version_request_t*>(
        message + transport_header_size);
    size_t spdm_request_size =
        message_size - transport_header_size -
        context->local_context.capability.transport_tail_size;

    LIBSPDM_ASSERT(spdm_request_size >= sizeof(spdm_get_version_request_t));
    spdm_request->header.spdm_version          = SPDM_MESSAGE_VERSION_10;
    spdm_request->header.request_response_code = SPDM_GET_VERSION;
    spdm_request->header.param1                = 0;
    spdm_request->header.param2                = 0;
    spdm_request_size = sizeof(spdm_get_version_request_t);

    // -----------------------------------------------------------------------
    // Send Request Phase
    // -----------------------------------------------------------------------
    status = co_await libspdm_send_request_async(
        context, nullptr, false, spdm_request_size, spdm_request, io);
    if (LIBSPDM_STATUS_IS_ERROR(status))
    {
        libspdm_release_sender_buffer(context);
        co_return status;
    }
    libspdm_release_sender_buffer(context);

    // After send, the canonical copy of the request lives in last_spdm_request
    // (written by libspdm_send_request_async before transport encode).
    spdm_request = reinterpret_cast<spdm_get_version_request_t*>(
        context->last_spdm_request);

    // -----------------------------------------------------------------------
    // Receive Response Phase
    // -----------------------------------------------------------------------
    status = libspdm_acquire_receiver_buffer(
        context, &message_size, as_void_pp(&message));
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    // RAII guard: release the receiver buffer on every co_return from here on.
    ScopeExit release_receiver([context] {
        libspdm_release_receiver_buffer(context);
    });

    LIBSPDM_ASSERT(message_size >= transport_header_size);
    auto* spdm_response =
        reinterpret_cast<libspdm_version_response_max_async_t*>(message);
    size_t spdm_response_size = message_size;

    status = co_await libspdm_receive_response_async(
        context, nullptr, false, &spdm_response_size,
        as_void_pp(&spdm_response), io);
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    // -----------------------------------------------------------------------
    // Validate Response Phase
    // -----------------------------------------------------------------------
    if (spdm_response_size < sizeof(spdm_message_header_t))
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    if (spdm_response->header.spdm_version != SPDM_MESSAGE_VERSION_10)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    if (spdm_response->header.request_response_code == SPDM_ERROR)
    {
        if (spdm_response->header.param1 == SPDM_ERROR_CODE_RESPONSE_NOT_READY)
            co_return LIBSPDM_STATUS_ERROR_PEER;

        status = libspdm_handle_simple_error_response(
            context, spdm_response->header.param1);
        if (LIBSPDM_STATUS_IS_ERROR(status))
            co_return status;
    }
    else if (spdm_response->header.request_response_code != SPDM_VERSION)
    {
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
    }

    if (spdm_response_size < sizeof(spdm_version_response_t))
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    // Both zero and out-of-range entry counts are invalid; one check suffices.
    const uint8_t entry_count = spdm_response->version_number_entry_count;
    if (entry_count == 0 || entry_count > LIBSPDM_MAX_VERSION_COUNT)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    const size_t entries_size =
        static_cast<size_t>(entry_count) * sizeof(spdm_version_number_t);
    if (spdm_response_size < sizeof(spdm_version_response_t) + entries_size)
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    spdm_response_size = sizeof(spdm_version_response_t) + entries_size;

    // -----------------------------------------------------------------------
    // Process Response Phase
    // -----------------------------------------------------------------------
    status = libspdm_append_message_a(context, spdm_request, spdm_request_size);
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    status = libspdm_append_message_a(context, spdm_response, spdm_response_size);
    if (LIBSPDM_STATUS_IS_ERROR(status))
    {
        libspdm_reset_message_a(context);
        co_return status;
    }

    spdm_version_number_t common_version{};
    const bool negotiated = libspdm_negotiate_connection_version(
        &common_version,
        context->local_context.version.spdm_version,
        context->local_context.version.spdm_version_count,
        spdm_response->version_number_entry,
        entry_count);
    if (!negotiated)
    {
        libspdm_reset_message_a(context);
        co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
    }

    libspdm_copy_mem(&context->connection_info.version,
                     sizeof(context->connection_info.version),
                     &common_version, sizeof(spdm_version_number_t));

    // -----------------------------------------------------------------------
    // Update State Phase
    // -----------------------------------------------------------------------
    // Set is_requester before advancing connection_state so that any observer
    // of connection_state sees a consistent view of both flags.
    context->local_context.is_requester = true;
    context->connection_info.connection_state =
        LIBSPDM_CONNECTION_STATE_AFTER_VERSION;

    // -----------------------------------------------------------------------
    // Log Message Phase
    // -----------------------------------------------------------------------
#if LIBSPDM_ENABLE_MSG_LOG
    libspdm_append_msg_log(context, spdm_response, spdm_response_size);
#endif

    co_return LIBSPDM_STATUS_SUCCESS;
    // release_receiver destructor fires here and on all earlier co_return paths.
}

} // namespace detail

/**
 * @brief Async version of libspdm_get_version().
 *
 * Outer retry wrapper matching upstream libspdm_get_version():
 * retries on LIBSPDM_STATUS_BUSY_PEER up to context->retry_times times,
 * with co_await timer delays instead of libspdm_sleep().
 */
template <AsyncSpdmIO IO>
boost::asio::awaitable<libspdm_return_t>
    libspdm_get_version_async(void* spdm_context, IO& io)
{
    auto* context = static_cast<libspdm_context_t*>(spdm_context);
    context->crypto_request = false;

    size_t   retry          = context->retry_times;
    uint64_t retry_delay_us = context->retry_delay_time;
    libspdm_return_t status;

    do
    {
        status = co_await detail::libspdm_try_get_version_async(context, io);
        if (status != LIBSPDM_STATUS_BUSY_PEER)
            co_return status;

        // Sleep between retries only when a positive delay is configured.
        // The `retry-- != 0` guard in while() prevents sleeping after the last
        // attempt, so no extra `retry != 0` test is needed here.
        if (retry_delay_us > 0)
        {
            auto executor = co_await boost::asio::this_coro::executor;
            boost::asio::steady_timer timer(executor);
            timer.expires_after(std::chrono::microseconds(retry_delay_us));
            co_await timer.async_wait(boost::asio::use_awaitable);
        }
    } while (retry-- != 0);

    co_return status;
}
