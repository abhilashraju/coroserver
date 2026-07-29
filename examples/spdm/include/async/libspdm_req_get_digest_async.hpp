#pragma once
/**
 * Async port of libspdm_get_digest().
 *
 * Sends GET_DIGESTS and receives DIGESTS entirely via co_await so the
 * io_context is never blocked.  Follows the same request/acquire/send/
 * receive/validate/release pattern established by libspdm_req_get_version_async.
 */

#include "libspdm_async_io.hpp"
#include "libspdm_async_utils.hpp"
#include "libspdm_req_send_receive_async.hpp"

#include <boost/asio/awaitable.hpp>

extern "C"
{
#include <internal/libspdm_common_lib.h>
#include <internal/libspdm_requester_lib.h>
#include <library/spdm_requester_lib.h>
}

#include <cstdint>

/**
 * @brief Async version of libspdm_get_digest().
 *
 * On success, slot_mask and digest buffer are populated identically to the
 * synchronous libspdm_get_digest().
 *
 * @param spdm_context   Initialised and negotiated SPDM context.
 * @param session_id     Pointer to session id (nullptr for no session).
 * @param slot_mask_out  Populated with the responder's certificate slot mask.
 * @param digest_out     Caller-supplied buffer for digest data
 *                       (must be at least SPDM_MAX_SLOT_COUNT * hash_size).
 * @param io             AsyncSpdmIO implementation.
 */
template <AsyncSpdmIO IO>
boost::asio::awaitable<libspdm_return_t>
    libspdm_get_digest_async(void* spdm_context, const uint32_t* session_id,
                             uint8_t* slot_mask_out, void* digest_out, IO& io)
{
    auto* context = static_cast<libspdm_context_t*>(spdm_context);

    const size_t transport_header_size =
        context->local_context.capability.transport_header_size;

    // ── Acquire sender buffer ──────────────────────────────────────────────
    uint8_t* message = nullptr;
    size_t message_size = 0;
    libspdm_return_t status =
        libspdm_acquire_sender_buffer(context, &message_size,
                                      detail::as_void_pp(&message));
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    LIBSPDM_ASSERT(message_size >= transport_header_size);

    auto* req = reinterpret_cast<spdm_get_digest_request_t*>(
        message + transport_header_size);
    const size_t req_size = sizeof(spdm_get_digest_request_t);

    req->header.spdm_version = libspdm_get_connection_version(context);
    req->header.request_response_code = SPDM_GET_DIGESTS;
    req->header.param1 = 0;
    req->header.param2 = 0;

    // ── Send ──────────────────────────────────────────────────────────────
    status = co_await libspdm_send_request_async(
        context, session_id, false, req_size, req, io);
    libspdm_release_sender_buffer(context);
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    // ── Acquire receiver buffer ────────────────────────────────────────────
    status = libspdm_acquire_receiver_buffer(
        context, &message_size, detail::as_void_pp(&message));
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    detail::ScopeExit release_rx([context] {
        libspdm_release_receiver_buffer(context);
    });

    auto* resp = reinterpret_cast<spdm_digest_response_t*>(message);
    size_t resp_size = message_size;

    status = co_await libspdm_receive_response_async(
        context, session_id, false, &resp_size,
        detail::as_void_pp(&resp), io);
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    // ── Validate ───────────────────────────────────────────────────────────
    if (resp_size < sizeof(spdm_digest_response_t))
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    if (resp->header.request_response_code != SPDM_DIGESTS)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    // ── Extract slot_mask and digest data ──────────────────────────────────
    if (slot_mask_out)
        *slot_mask_out = resp->header.param2;

    const size_t hash_size = libspdm_get_hash_size(
        context->connection_info.algorithm.base_hash_algo);
    const size_t num_slots =
        static_cast<size_t>(__builtin_popcount(resp->header.param2));
    const size_t digest_data_size = num_slots * hash_size;

    if (resp_size < sizeof(spdm_digest_response_t) + digest_data_size)
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    if (digest_out && digest_data_size > 0)
    {
        libspdm_copy_mem(digest_out, digest_data_size,
                         reinterpret_cast<const uint8_t*>(resp) +
                             sizeof(spdm_digest_response_t),
                         digest_data_size);
    }

    co_return LIBSPDM_STATUS_SUCCESS;
}
