#pragma once
/**
 * Async port of libspdm_get_certificate().
 *
 * Sends GET_CERTIFICATE / receives CERTIFICATE in a loop (handling chunked
 * responses) entirely via co_await — the io_context is never blocked.
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
 * @brief Async version of libspdm_get_certificate().
 *
 * Retrieves the full certificate chain for the given slot, handling chunked
 * responses transparently. On success, cert_chain is populated with the raw
 * DER certificate chain bytes (header + hash + certs).
 *
 * @param spdm_context  Initialised and negotiated SPDM context.
 * @param session_id    Pointer to session id (nullptr for no session).
 * @param slot_id       Certificate slot to retrieve (0–7).
 * @param cert_chain    Output: receives the raw certificate chain bytes.
 * @param io            AsyncSpdmIO implementation.
 */
template <AsyncSpdmIO IO>
boost::asio::awaitable<libspdm_return_t>
    libspdm_get_certificate_async(void* spdm_context,
                                  const uint32_t* session_id, uint8_t slot_id,
                                  std::vector<uint8_t>& cert_chain, IO& io)
{
    auto* context = static_cast<libspdm_context_t*>(spdm_context);

    const size_t transport_header_size =
        context->local_context.capability.transport_header_size;

    cert_chain.clear();

    uint16_t offset = 0;
    static constexpr uint16_t PORTION_LENGTH = 0x400; // 1 KiB chunks

    while (true)
    {
        // ── Acquire sender buffer ──────────────────────────────────────────
        uint8_t* message = nullptr;
        size_t message_size = 0;
        libspdm_return_t status =
            libspdm_acquire_sender_buffer(context, &message_size,
                                          detail::as_void_pp(&message));
        if (LIBSPDM_STATUS_IS_ERROR(status))
            co_return status;

        LIBSPDM_ASSERT(message_size >= transport_header_size);

        auto* req = reinterpret_cast<spdm_get_certificate_request_t*>(
            message + transport_header_size);
        const size_t req_size = sizeof(spdm_get_certificate_request_t);

        req->header.spdm_version = libspdm_get_connection_version(context);
        req->header.request_response_code = SPDM_GET_CERTIFICATE;
        req->header.param1 = slot_id;
        req->header.param2 = 0;
        req->offset = offset;
        req->length = PORTION_LENGTH;

        // ── Send ────────────────────────────────────────────────────────────
        status = co_await libspdm_send_request_async(
            context, session_id, false, req_size, req, io);
        libspdm_release_sender_buffer(context);
        if (LIBSPDM_STATUS_IS_ERROR(status))
            co_return status;

        // ── Acquire receiver buffer ────────────────────────────────────────
        status = libspdm_acquire_receiver_buffer(
            context, &message_size, detail::as_void_pp(&message));
        if (LIBSPDM_STATUS_IS_ERROR(status))
            co_return status;

        detail::ScopeExit release_rx([context] {
            libspdm_release_receiver_buffer(context);
        });

        auto* resp = reinterpret_cast<spdm_certificate_response_t*>(message);
        size_t resp_size = message_size;

        status = co_await libspdm_receive_response_async(
            context, session_id, false, &resp_size,
            detail::as_void_pp(&resp), io);
        if (LIBSPDM_STATUS_IS_ERROR(status))
            co_return status;

        // ── Validate ──────────────────────────────────────────────────────
        if (resp_size < sizeof(spdm_certificate_response_t))
            co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

        if (resp->header.request_response_code != SPDM_CERTIFICATE)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

        if (resp->header.param1 != slot_id)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

        const uint16_t portion_len = resp->portion_length;
        const uint16_t remainder_len = resp->remainder_length;

        if (resp_size < sizeof(spdm_certificate_response_t) + portion_len)
            co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

        // ── Accumulate portion ────────────────────────────────────────────
        const auto* data_ptr =
            reinterpret_cast<const uint8_t*>(resp) +
            sizeof(spdm_certificate_response_t);
        cert_chain.insert(cert_chain.end(), data_ptr, data_ptr + portion_len);

        offset = static_cast<uint16_t>(offset + portion_len);

        if (remainder_len == 0)
            break; // full chain received
    }

    co_return LIBSPDM_STATUS_SUCCESS;
}
