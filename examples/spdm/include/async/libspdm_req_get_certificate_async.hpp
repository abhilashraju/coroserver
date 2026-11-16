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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

/**
 * @brief Async version of libspdm_get_certificate().
 *
 * Retrieves the full certificate chain for the given slot, handling chunked
 * responses transparently. On success, cert_chain is populated with the raw
 * certificate chain bytes (spdm_cert_chain_t header + root hash + DER certs).
 *
 * The chain header length and root certificate hash are verified, the
 * exchange is appended to transcript B and the chain is cached as the peer
 * used chain of `slot_id`, so later signature verification (e.g. signed
 * GET_MEASUREMENTS) can use the leaf public key.
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
    uint16_t remaining = 0;
    libspdm_return_t status = LIBSPDM_STATUS_SUCCESS;
    static constexpr uint16_t PORTION_LENGTH = 0x400; // 1 KiB chunks

    if (slot_id >= SPDM_MAX_SLOT_COUNT)
        co_return LIBSPDM_STATUS_INVALID_PARAMETER;
    if (context->connection_info.connection_state <
        LIBSPDM_CONNECTION_STATE_NEGOTIATED)
        co_return LIBSPDM_STATUS_INVALID_STATE_LOCAL;

    if (session_id == nullptr)
        libspdm_reset_message_buffer_via_request_code(
            context, nullptr, SPDM_GET_CERTIFICATE);

    while (true)
    {
        // ── Acquire sender buffer ──────────────────────────────────────────
        uint8_t* message = nullptr;
        size_t message_size = 0;
        status = libspdm_acquire_sender_buffer(context, &message_size,
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
        req->length = offset == 0 ? PORTION_LENGTH
                                  : std::min(PORTION_LENGTH, remaining);
        const uint8_t reqVersion = req->header.spdm_version;
        const uint16_t reqLength = req->length;

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
        if (resp_size < sizeof(spdm_message_header_t))
            co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

        if (resp->header.request_response_code == SPDM_ERROR)
        {
            status = libspdm_handle_simple_error_response(
                context, resp->header.param1);
            co_return LIBSPDM_STATUS_IS_ERROR(status)
                ? status
                : LIBSPDM_STATUS_INVALID_MSG_FIELD;
        }

        if (resp_size < sizeof(spdm_certificate_response_t))
            co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

        if (resp->header.request_response_code != SPDM_CERTIFICATE)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

        if (resp->header.spdm_version != reqVersion)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

        if ((resp->header.param1 & SPDM_CERTIFICATE_RESPONSE_SLOT_ID_MASK) !=
            slot_id)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

        const uint16_t portion_len = resp->portion_length;
        const uint16_t remainder_len = resp->remainder_length;

        if (portion_len == 0 || portion_len > reqLength)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

        if (resp_size < sizeof(spdm_certificate_response_t) + portion_len)
            co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

        if (static_cast<uint32_t>(offset) + portion_len + remainder_len >
            SPDM_MAX_CERTIFICATE_CHAIN_SIZE)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

        // ── Transcript B (the request is kept in last_spdm_request) ───────
        if (session_id == nullptr)
        {
            status = libspdm_append_message_b(
                context, context->last_spdm_request, req_size);
            if (LIBSPDM_STATUS_IS_ERROR(status))
                co_return status;
            status = libspdm_append_message_b(
                context, resp, sizeof(spdm_certificate_response_t) + portion_len);
            if (LIBSPDM_STATUS_IS_ERROR(status))
                co_return status;
        }

        // ── Accumulate portion ────────────────────────────────────────────
        const auto* data_ptr =
            reinterpret_cast<const uint8_t*>(resp) +
            sizeof(spdm_certificate_response_t);
        cert_chain.insert(cert_chain.end(), data_ptr, data_ptr + portion_len);

        offset = static_cast<uint16_t>(offset + portion_len);
        remaining = remainder_len;

        if (context->connection_info.connection_state <
            LIBSPDM_CONNECTION_STATE_AFTER_CERTIFICATE)
            context->connection_info.connection_state =
                LIBSPDM_CONNECTION_STATE_AFTER_CERTIFICATE;

        if (remainder_len == 0)
            break; // full chain received
    }

    // ── Verify chain integrity: header length and root certificate hash ───
    const uint32_t baseHashAlgo =
        context->connection_info.algorithm.base_hash_algo;
    const size_t hashSize = libspdm_get_hash_size(baseHashAlgo);
    if (cert_chain.size() <= sizeof(spdm_cert_chain_t) + hashSize)
        co_return LIBSPDM_STATUS_VERIF_FAIL;

    spdm_cert_chain_t header;
    std::memcpy(&header, cert_chain.data(), sizeof(header));
    if (header.length != cert_chain.size())
        co_return LIBSPDM_STATUS_VERIF_FAIL;

    const uint8_t* certs =
        cert_chain.data() + sizeof(spdm_cert_chain_t) + hashSize;
    const size_t certsSize =
        cert_chain.size() - sizeof(spdm_cert_chain_t) - hashSize;
    const uint8_t* rootCert = nullptr;
    size_t rootCertSize = 0;
    if (!libspdm_x509_get_cert_from_cert_chain(certs, certsSize, 0, &rootCert,
                                               &rootCertSize))
        co_return LIBSPDM_STATUS_VERIF_FAIL;

    uint8_t rootHash[LIBSPDM_MAX_HASH_SIZE];
    if (!libspdm_hash_all(baseHashAlgo, rootCert, rootCertSize, rootHash))
        co_return LIBSPDM_STATUS_CRYPTO_ERROR;
    if (!libspdm_consttime_is_mem_equal(
            cert_chain.data() + sizeof(spdm_cert_chain_t), rootHash, hashSize))
        co_return LIBSPDM_STATUS_VERIF_FAIL;

    // ── Cache as the peer used chain (hash + leaf public key) ─────────────
    libspdm_data_parameter_t parameter;
    libspdm_zero_mem(&parameter, sizeof(parameter));
    parameter.location = LIBSPDM_DATA_LOCATION_CONNECTION;
    parameter.additional_data[0] = slot_id;
    status = libspdm_set_data(spdm_context,
                              LIBSPDM_DATA_PEER_USED_CERT_CHAIN_BUFFER,
                              &parameter, cert_chain.data(), cert_chain.size());
    co_return status;
}
