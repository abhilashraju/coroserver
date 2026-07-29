#pragma once
/**
 * Async port of libspdm_responder_dispatch_message().
 *
 * This is a direct C++ coroutine translation of the C original in
 * library/spdm_responder_lib/libspdm_rsp_communication.c (lines 9-80).
 *
 * The only two lines that change:
 *   line 37: context->receive_message(...)  →  co_await io.receive(...)
 *   line 75: context->send_message(...)     →  co_await io.send(...)
 *
 * All other work (buffer acquire/release, libspdm_process_request,
 * libspdm_build_response) is pure CPU computation and is called directly.
 * These functions are safe to call from a coroutine because they do not block.
 */

#include "libspdm_async_io.hpp"

#include <boost/asio/awaitable.hpp>

extern "C"
{
#include <internal/libspdm_common_lib.h>
#include <internal/libspdm_responder_lib.h>
}

/**
 * @brief Async version of libspdm_responder_dispatch_message().
 *
 * Receives one SPDM request, processes it, builds the response, and sends it.
 * Each call handles exactly one request-response exchange; call in a loop for
 * continuous serving.
 *
 * @param spdm_context  Initialised and configured libspdm responder context.
 * @param io            AsyncSpdmIO provider for the current connection.
 * @return LIBSPDM_STATUS_SUCCESS on a successful exchange, or a failure status.
 */
template <AsyncSpdmIO IO>
boost::asio::awaitable<libspdm_return_t>
    libspdm_responder_dispatch_message_async(void* spdm_context, IO& io)
{
    libspdm_return_t status;
    libspdm_context_t* context = static_cast<libspdm_context_t*>(spdm_context);

    uint8_t* request = nullptr;
    size_t request_size = 0;
    uint8_t* response = nullptr;
    size_t response_size = 0;
    uint32_t tmp_session_id = 0;
    uint32_t* session_id = nullptr;
    uint32_t* session_id_ptr = nullptr;
    bool is_app_message = false;
    void* message = nullptr;
    size_t message_size = 0;

    /* ── Acquire receiver buffer ── */
    status = libspdm_acquire_receiver_buffer(context, &message_size,
                                             &message);
    if (LIBSPDM_STATUS_IS_ERROR(status))
    {
        co_return status;
    }
    request = static_cast<uint8_t*>(message);
    request_size = message_size;

#if LIBSPDM_ENABLE_CAPABILITY_CHUNK_CAP
    libspdm_get_receiver_buffer(context, reinterpret_cast<void**>(&request),
                                &request_size);
#endif

    /* ── ASYNC I/O ── replaces: context->receive_message(...) (line 37) */
    if (!co_await io.receive(request, request_size, 0))
    {
        libspdm_release_receiver_buffer(context);
        co_return LIBSPDM_STATUS_RECEIVE_FAIL;
    }

    /* ── Process the received request (CPU only) ── */
    status = libspdm_process_request(context, &session_id, &is_app_message,
                                     request_size, request);
    if (LIBSPDM_STATUS_IS_ERROR(status))
    {
        libspdm_release_receiver_buffer(context);
        co_return status;
    }

    /* Save session_id before releasing the receiver buffer */
    if (session_id != nullptr)
    {
        tmp_session_id = *session_id;
        session_id_ptr = &tmp_session_id;
    }
    else
    {
        session_id_ptr = nullptr;
    }

    libspdm_release_receiver_buffer(context);

    /* ── Acquire sender buffer and build the response (CPU only) ── */
    status = libspdm_acquire_sender_buffer(context, &message_size, &message);
    if (LIBSPDM_STATUS_IS_ERROR(status))
    {
        co_return status;
    }
    response = static_cast<uint8_t*>(message);
    response_size = message_size;
    libspdm_zero_mem(response, response_size);

    status = libspdm_build_response(context, session_id_ptr, is_app_message,
                                    &response_size,
                                    reinterpret_cast<void**>(&response));
    if (LIBSPDM_STATUS_IS_ERROR(status))
    {
        libspdm_release_sender_buffer(context);
        co_return status;
    }

    /* ── ASYNC I/O ── replaces: context->send_message(...) (line 75) */
    if (!co_await io.send(response, response_size, 0))
    {
        libspdm_release_sender_buffer(context);
        co_return LIBSPDM_STATUS_SEND_FAIL;
    }

    libspdm_release_sender_buffer(context);
    co_return LIBSPDM_STATUS_SUCCESS;
}
