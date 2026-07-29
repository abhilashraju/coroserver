#pragma once
/**
 * Async port of libspdm_get_measurement().
 *
 * Sends GET_MEASUREMENTS and receives MEASUREMENTS entirely via co_await.
 * Returns the raw measurement record bytes which callers can encode to base64.
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
#include <vector>

/**
 * @brief Async version of libspdm_get_measurement().
 *
 * @param spdm_context        Initialised and negotiated SPDM context.
 * @param session_id          Pointer to session id (nullptr for no session).
 * @param request_attribute   Measurement request attribute flags.
 * @param measurement_operation  Index (0xFF = all, 0 = count, 1-254 = specific).
 * @param slot_id             Certificate slot id for signature verification.
 * @param number_of_blocks    Output: number of measurement blocks returned.
 * @param measurements_out    Output: raw measurement record bytes.
 * @param io                  AsyncSpdmIO implementation.
 */
template <AsyncSpdmIO IO>
boost::asio::awaitable<libspdm_return_t>
    libspdm_get_measurement_async(void* spdm_context,
                                  const uint32_t* session_id,
                                  uint8_t request_attribute,
                                  uint8_t measurement_operation,
                                  uint8_t slot_id,
                                  uint8_t* number_of_blocks,
                                  std::vector<uint8_t>& measurements_out,
                                  IO& io)
{
    auto* context = static_cast<libspdm_context_t*>(spdm_context);

    const size_t transport_header_size =
        context->local_context.capability.transport_header_size;

    measurements_out.clear();

    // ── Acquire sender buffer ──────────────────────────────────────────────
    uint8_t* message = nullptr;
    size_t message_size = 0;
    libspdm_return_t status =
        libspdm_acquire_sender_buffer(context, &message_size,
                                      detail::as_void_pp(&message));
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    LIBSPDM_ASSERT(message_size >= transport_header_size);

    auto* req = reinterpret_cast<spdm_get_measurements_request_t*>(
        message + transport_header_size);
    size_t req_size = sizeof(spdm_get_measurements_request_t);

    req->header.spdm_version = libspdm_get_connection_version(context);
    req->header.request_response_code = SPDM_GET_MEASUREMENTS;
    req->header.param1 = request_attribute;
    req->header.param2 = measurement_operation;

    // Include SlotIDParam only when a signature is requested
    if (request_attribute &
        SPDM_GET_MEASUREMENTS_REQUEST_ATTRIBUTES_GENERATE_SIGNATURE)
    {
        req->slot_id_param = slot_id;
        req_size = sizeof(spdm_get_measurements_request_t);
    }
    else
    {
        // No nonce / slot when no signature is requested — trim trailing fields
        req_size = sizeof(spdm_message_header_t);
    }

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

    auto* resp = reinterpret_cast<spdm_measurements_response_t*>(message);
    size_t resp_size = message_size;

    status = co_await libspdm_receive_response_async(
        context, session_id, false, &resp_size,
        detail::as_void_pp(&resp), io);
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    // ── Validate ───────────────────────────────────────────────────────────
    if (resp_size < sizeof(spdm_measurements_response_t))
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    if (resp->header.request_response_code != SPDM_MEASUREMENTS)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    if (number_of_blocks)
        *number_of_blocks = resp->number_of_blocks;

    // ── Extract measurement record ─────────────────────────────────────────
    // measurement_record_length is a 3-byte little-endian field in the SPDM spec
    const uint32_t record_length =
        static_cast<uint32_t>(resp->measurement_record_length[0]) |
        (static_cast<uint32_t>(resp->measurement_record_length[1]) << 8) |
        (static_cast<uint32_t>(resp->measurement_record_length[2]) << 16);

    if (resp_size < sizeof(spdm_measurements_response_t) + record_length)
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    const auto* record_ptr =
        reinterpret_cast<const uint8_t*>(resp) +
        sizeof(spdm_measurements_response_t);
    measurements_out.assign(record_ptr, record_ptr + record_length);

    co_return LIBSPDM_STATUS_SUCCESS;
}
