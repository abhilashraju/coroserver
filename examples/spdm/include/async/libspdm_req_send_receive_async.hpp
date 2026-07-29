#pragma once
/**
 * Async ports of libspdm_send_request() and libspdm_receive_response().
 *
 * These are direct C++ coroutine translations of the C originals in
 * library/spdm_requester_lib/libspdm_req_send_receive.c.
 *
 * The only change from the C originals is that the two blocking call sites:
 *   context->send_message(...)    (line 113)
 *   context->receive_message(...) (line 156)
 * are replaced with:
 *   co_await io.send(...)
 *   co_await io.receive(...)
 *
 * All other logic (buffer selection, transport encode/decode, session handling,
 * key-update rollback) is preserved verbatim.
 */

#include "libspdm_async_io.hpp"

#include <boost/asio/awaitable.hpp>

extern "C"
{
#include <internal/libspdm_common_lib.h>
#include <internal/libspdm_requester_lib.h>
#include <internal/libspdm_secured_message_lib.h>
}

/**
 * @brief Async version of libspdm_send_request.
 *
 * Encodes the SPDM request into a transport message and sends it via io.send().
 * Corresponds to libspdm_req_send_receive.c lines 10-121.
 */
template <AsyncSpdmIO IO>
boost::asio::awaitable<libspdm_return_t>
    libspdm_send_request_async(void* spdm_context, const uint32_t* session_id,
                                bool is_app_message, size_t request_size,
                                void* request, IO& io)
{
    libspdm_context_t* context = static_cast<libspdm_context_t*>(spdm_context);
    libspdm_return_t status;
    uint8_t* message = nullptr;
    size_t message_size = 0;
    uint64_t timeout;
    uint8_t* scratch_buffer = nullptr;
    size_t scratch_buffer_size = 0;
    size_t transport_header_size;
    uint8_t* sender_buffer = nullptr;
    size_t sender_buffer_size = 0;

    transport_header_size = context->local_context.capability.transport_header_size;
    libspdm_get_scratch_buffer(context, reinterpret_cast<void**>(&scratch_buffer),
                               &scratch_buffer_size);
    libspdm_get_sender_buffer(context, reinterpret_cast<void**>(&sender_buffer),
                              &sender_buffer_size);

#if LIBSPDM_ENABLE_CAPABILITY_CHUNK_CAP
    // These helpers require libspdm_context_t*; use the already-cast `context`.
    if (reinterpret_cast<uint8_t*>(request) >= sender_buffer &&
        reinterpret_cast<uint8_t*>(request) < sender_buffer + sender_buffer_size)
    {
        message = sender_buffer;
        message_size = sender_buffer_size;
    }
    else
    {
        if (reinterpret_cast<uint8_t*>(request) >=
                scratch_buffer +
                    libspdm_get_scratch_buffer_sender_receiver_offset(
                        context) &&
            reinterpret_cast<uint8_t*>(request) <
                scratch_buffer +
                    libspdm_get_scratch_buffer_sender_receiver_offset(
                        context) +
                    libspdm_get_scratch_buffer_sender_receiver_capacity(
                        context))
        {
            message = scratch_buffer +
                      libspdm_get_scratch_buffer_sender_receiver_offset(
                          context);
            message_size =
                libspdm_get_scratch_buffer_sender_receiver_capacity(
                    context);
        }
        else if (reinterpret_cast<uint8_t*>(request) >=
                     scratch_buffer +
                         libspdm_get_scratch_buffer_large_sender_receiver_offset(
                             context) &&
                 reinterpret_cast<uint8_t*>(request) <
                     scratch_buffer +
                         libspdm_get_scratch_buffer_large_sender_receiver_offset(
                             context) +
                         libspdm_get_scratch_buffer_large_sender_receiver_capacity(
                             context))
        {
            message = scratch_buffer +
                      libspdm_get_scratch_buffer_large_sender_receiver_offset(
                          context);
            message_size =
                libspdm_get_scratch_buffer_large_sender_receiver_capacity(
                    context);
        }
    }
#else
    message = sender_buffer;
    message_size = sender_buffer_size;
#endif

    if (session_id != nullptr)
    {
        libspdm_copy_mem(scratch_buffer + transport_header_size,
                         scratch_buffer_size - transport_header_size, request,
                         request_size);
        request = scratch_buffer + transport_header_size;
    }

    /* backup last request for RESPOND_IF_READY comparison */
    const auto* req_hdr =
        static_cast<const spdm_message_header_t*>(request);
    if (req_hdr->request_response_code != SPDM_RESPOND_IF_READY &&
        req_hdr->request_response_code != SPDM_CHUNK_GET &&
        req_hdr->request_response_code != SPDM_CHUNK_SEND)
    {
        libspdm_copy_mem(
            context->last_spdm_request,
            libspdm_get_scratch_buffer_last_spdm_request_capacity(context),
            request, request_size);
        context->last_spdm_request_size = request_size;
    }

    status = context->transport_encode_message(context, session_id,
                                               is_app_message, true,
                                               request_size, request,
                                               &message_size,
                                               reinterpret_cast<void**>(&message));
    if (session_id != nullptr)
    {
        libspdm_zero_mem(request, request_size);
    }
    if (LIBSPDM_STATUS_IS_ERROR(status))
    {
        if (session_id != nullptr &&
            (status == LIBSPDM_STATUS_SEQUENCE_NUMBER_OVERFLOW ||
             status == LIBSPDM_STATUS_CRYPTO_ERROR))
        {
            libspdm_free_session_id(context, *session_id);
        }
        co_return status;
    }

    timeout = context->local_context.capability.rtt;

    /* ── ASYNC I/O ── replaces: context->send_message(...) */
    if (!co_await io.send(message, message_size, timeout))
    {
        co_return LIBSPDM_STATUS_SEND_FAIL;
    }

    co_return LIBSPDM_STATUS_SUCCESS;
}

/**
 * @brief Async version of libspdm_receive_response.
 *
 * Reads a transport message via io.receive(), then decodes it into an SPDM
 * response.  Corresponds to libspdm_req_send_receive.c lines 123-323.
 */
template <AsyncSpdmIO IO>
boost::asio::awaitable<libspdm_return_t>
    libspdm_receive_response_async(void* spdm_context,
                                   const uint32_t* session_id,
                                   bool is_app_message, size_t* response_size,
                                   void** response, IO& io)
{
    libspdm_context_t* context = static_cast<libspdm_context_t*>(spdm_context);
    void* temp_session_context = nullptr;
    libspdm_return_t status;
    uint8_t* message = nullptr;
    size_t message_size = 0;
    uint32_t* message_session_id = nullptr;
    uint32_t message_id = 0;
    bool is_message_app_message = false;
    uint64_t timeout;
    size_t transport_header_size;
    uint8_t* scratch_buffer = nullptr;
    size_t scratch_buffer_size = 0;
    void* backup_response = nullptr;
    size_t backup_response_size = 0;
    bool reset_key_update = false;
    bool result;

    if (context->crypto_request)
    {
        timeout = context->local_context.capability.rtt +
                  (static_cast<uint64_t>(1)
                   << context->connection_info.capability.ct_exponent);
    }
    else
    {
        timeout = context->local_context.capability.rtt +
                  context->local_context.capability.st1;
    }

    message = static_cast<uint8_t*>(*response);
    message_size = *response_size;

    /* ── ASYNC I/O ── replaces: context->receive_message(...) */
    if (!co_await io.receive(message, message_size, timeout))
    {
        co_return LIBSPDM_STATUS_RECEIVE_FAIL;
    }

    if (session_id != nullptr)
    {
        message_session_id = &message_id;
        message_id = *session_id;
    }
    is_message_app_message = false;

    transport_header_size = context->local_context.capability.transport_header_size;
    libspdm_get_scratch_buffer(context, reinterpret_cast<void**>(&scratch_buffer),
                               &scratch_buffer_size);
#if LIBSPDM_ENABLE_CAPABILITY_CHUNK_CAP
    *response = scratch_buffer +
                libspdm_get_scratch_buffer_secure_message_offset() +
                transport_header_size;
    *response_size =
        libspdm_get_scratch_buffer_secure_message_capacity(context) -
        transport_header_size;
#else
    *response = scratch_buffer + transport_header_size;
    *response_size = scratch_buffer_size - transport_header_size;
#endif

    backup_response = *response;
    backup_response_size = *response_size;

    status = context->transport_decode_message(context, &message_session_id,
                                               &is_message_app_message, false,
                                               message_size, message,
                                               response_size, response);

    reset_key_update = false;
    temp_session_context = nullptr;

    if (status == LIBSPDM_STATUS_SESSION_TRY_DISCARD_KEY_UPDATE)
    {
        if (message_session_id == nullptr)
        {
            co_return LIBSPDM_STATUS_INVALID_STATE_LOCAL;
        }
        temp_session_context =
            libspdm_get_secured_message_context_via_session_id(
                context, *message_session_id);
        if (temp_session_context == nullptr)
        {
            co_return LIBSPDM_STATUS_INVALID_STATE_LOCAL;
        }
        result = libspdm_activate_update_session_data_key(
            temp_session_context, LIBSPDM_KEY_UPDATE_ACTION_RESPONDER, false);
        if (!result)
        {
            co_return LIBSPDM_STATUS_INVALID_STATE_LOCAL;
        }
        if (session_id != nullptr)
        {
            *message_session_id = *session_id;
        }
        else
        {
            message_session_id = nullptr;
        }
        is_message_app_message = false;
        *response = backup_response;
        *response_size = backup_response_size;
        status = context->transport_decode_message(
            context, &message_session_id, &is_message_app_message, false,
            message_size, message, response_size, response);
        reset_key_update = true;
    }

    *response_size = LIBSPDM_MIN(
        *response_size,
        context->local_context.capability.data_transfer_size);

    /* session_id / is_app_message validation — same as C original */
    if (session_id != nullptr)
    {
        if (message_session_id == nullptr)
        {
            goto error;
        }
        if (*message_session_id != *session_id)
        {
            goto error;
        }
    }
    else
    {
        if (message_session_id != nullptr)
        {
            goto error;
        }
    }

    if ((is_app_message && !is_message_app_message) ||
        (!is_app_message && is_message_app_message))
    {
        goto error;
    }

    if (LIBSPDM_STATUS_IS_ERROR(status))
    {
        if (session_id != nullptr &&
            context->last_spdm_error.error_code ==
                SPDM_ERROR_CODE_DECRYPT_ERROR)
        {
            libspdm_free_session_id(context, *session_id);
        }
    }

    if (reset_key_update)
    {
        if (temp_session_context == nullptr || message_session_id == nullptr)
        {
            co_return LIBSPDM_STATUS_INVALID_STATE_LOCAL;
        }
        result = libspdm_create_update_session_data_key(
            temp_session_context, LIBSPDM_KEY_UPDATE_ACTION_RESPONDER);
        if (!result)
        {
            co_return LIBSPDM_STATUS_INVALID_STATE_LOCAL;
        }
    }

    co_return status;

error:
    if (context->last_spdm_error.error_code == SPDM_ERROR_CODE_DECRYPT_ERROR)
    {
        co_return LIBSPDM_STATUS_SESSION_MSG_ERROR;
    }
    co_return LIBSPDM_STATUS_RECEIVE_FAIL;
}
