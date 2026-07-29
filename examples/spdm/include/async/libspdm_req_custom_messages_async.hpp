#pragma once
/**
 * Async custom-message exchange helpers.
 *
 * Provides co_await-friendly versions of the three vendor-defined messages
 * used by the requester:
 *   - asyncPushCertificate   — PUSH_CERTIFICATE request → ACK
 *   - asyncPullCertificate   — PULL_CERTIFICATE request → certificate bytes
 *   - asyncSetProvisioned    — SET_PROVISIONED request → ACK
 *
 * These messages are sent through the proper SPDM channel:
 *   libspdm_send_request_async   — encodes the TCP binding header via
 *                                  transport_encode_message, then calls io.send()
 *   libspdm_receive_response_async — calls io.receive() then strips the header
 *                                    via transport_decode_message
 *
 * On the responder side libspdm_process_request cannot find a handler for
 * opcodes 0x7D/0x7E/0x7F (they are vendor-defined, not standard SPDM), so
 * it falls through to the libspdm_get_response_func callback registered via
 * libspdm_register_get_response_func().  That callback dispatches to the
 * application's custom handler — no rawSend/rawReceive bypass needed.
 *
 * Constraint: session_id must be nullptr (plain-text TCP channel).
 * is_app_message is false because the payload is placed in the normal SPDM
 * message slot, not inside an encrypted session app-data envelope.
 */

#include "async_spdm_io.hpp"
#include "libspdm_req_send_receive_async.hpp"

#include <boost/asio/awaitable.hpp>

#include "spdm_custom_messages.hpp"

extern "C"
{
#include <internal/libspdm_common_lib.h>
}

#include <cstdint>
#include <vector>

// ── Transport-layer send/receive helpers ──────────────────────────────────

/**
 * @brief Send a custom vendor payload through the SPDM channel.
 *
 * The payload is placed into libspdm's sender buffer at the offset after
 * the transport header reservation, then libspdm_send_request_async runs
 * transport_encode_message (prepends the 4-byte TCP binding header) and
 * calls io.send().  This is identical to how standard SPDM requests are sent.
 *
 * @param spdmContext  Initialised libspdm requester context.
 * @param io           AsyncSpdmTcpIO for the current connection.
 * @param payload      Serialised custom message bytes (no framing).
 */
inline boost::asio::awaitable<bool>
    customSend(void* spdmContext, AsyncSpdmTcpIO& io,
               const std::vector<uint8_t>& payload)
{
    auto* ctx = static_cast<libspdm_context_t*>(spdmContext);
    const size_t hdr = ctx->local_context.capability.transport_header_size;

    void* msg_buf = nullptr;
    size_t msg_size = 0;
    if (LIBSPDM_STATUS_IS_ERROR(
            libspdm_acquire_sender_buffer(ctx, &msg_size, &msg_buf)))
        co_return false;

    if (payload.size() > msg_size - hdr)
    {
        libspdm_release_sender_buffer(ctx);
        co_return false;
    }

    // Write payload after the transport header reservation.
    auto* slot = static_cast<uint8_t*>(msg_buf) + hdr;
    std::memcpy(slot, payload.data(), payload.size());
    libspdm_release_sender_buffer(ctx);

    // libspdm_send_request_async encodes (adds TCP binding header) and sends.
    auto status = co_await libspdm_send_request_async(
        spdmContext,
        /*session_id=*/nullptr,
        /*is_app_message=*/false,
        payload.size(), slot, io);

    co_return !LIBSPDM_STATUS_IS_ERROR(status);
}

/**
 * @brief Receive a fixed-size custom vendor response through the SPDM channel.
 *
 * libspdm_receive_response_async calls io.receive() which reads the TCP
 * binding header and payload, then transport_decode_message strips the header
 * and puts the decoded payload into the scratch buffer.  We copy
 * `expected_size` bytes out of that buffer into `out`.
 *
 * @param spdmContext    Initialised libspdm requester context.
 * @param io             AsyncSpdmTcpIO for the current connection.
 * @param out            Receives the decoded payload bytes.
 * @param expected_size  Number of bytes expected in the response payload.
 */
inline boost::asio::awaitable<bool>
    customReceive(void* spdmContext, AsyncSpdmTcpIO& io,
                  std::vector<uint8_t>& out, size_t expected_size)
{
    auto* ctx = static_cast<libspdm_context_t*>(spdmContext);
    const size_t hdr = ctx->local_context.capability.transport_header_size;

    uint8_t* scratch = nullptr;
    size_t scratch_size = 0;
    libspdm_get_scratch_buffer(ctx, reinterpret_cast<void**>(&scratch),
                               &scratch_size);

    void* response = scratch + hdr;
    size_t response_size = scratch_size - hdr;

    auto status = co_await libspdm_receive_response_async(
        spdmContext,
        /*session_id=*/nullptr,
        /*is_app_message=*/false,
        &response_size, &response, io);

    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return false;

    if (response_size < expected_size)
        co_return false;

    out.assign(static_cast<uint8_t*>(response),
               static_cast<uint8_t*>(response) + expected_size);
    co_return true;
}

// ── asyncPushCertificate ───────────────────────────────────────────────────

/**
 * @brief Async PUSH_CERTIFICATE: send requester cert to responder.
 *
 * @param spdmContext  Initialised libspdm requester context.
 */
inline boost::asio::awaitable<bool>
    asyncPushCertificate(const std::vector<uint8_t>& cert,
                         CertificateFormat format, void* spdmContext,
                         AsyncSpdmTcpIO& io)
{
    auto request =
        spdm_serialization::createPushCertificateRequest(0x12, cert, format);

    if (!co_await customSend(spdmContext, io, request))
        co_return false;

    std::vector<uint8_t> response;
    if (!co_await customReceive(spdmContext, io, response,
                                sizeof(SpdmPushCertificateResponse)))
        co_return false;

    SpdmPushCertificateResponse resp{};
    std::memcpy(&resp, response.data(), sizeof(resp));
    co_return resp.status == CERT_EXCHANGE_SUCCESS;
}

// ── asyncPullCertificate ───────────────────────────────────────────────────

/**
 * @brief Async PULL_CERTIFICATE: retrieve responder cert.
 *
 * @param spdmContext  Initialised libspdm requester context.
 */
inline boost::asio::awaitable<bool>
    asyncPullCertificate(CertificateFormat format,
                         std::vector<uint8_t>& cert_out, void* spdmContext,
                         AsyncSpdmTcpIO& io)
{
    auto request =
        spdm_serialization::createPullCertificateRequest(0x12, format);

    if (!co_await customSend(spdmContext, io, request))
        co_return false;

    // Receive the fixed-size response header to learn the cert size.
    std::vector<uint8_t> header;
    if (!co_await customReceive(spdmContext, io, header,
                                sizeof(SpdmPullCertificateResponse)))
        co_return false;

    try
    {
        size_t offset = 0;
        auto resp =
            spdm_serialization::deserializePOD<SpdmPullCertificateResponse>(
                header, offset);

        if (resp.status != CERT_EXCHANGE_SUCCESS)
            co_return false;

        // The variable-length cert arrives as a second SPDM-framed message.
        if (!co_await customReceive(spdmContext, io, cert_out, resp.cert_size))
            co_return false;

        co_return true;
    }
    catch (...)
    {
        co_return false;
    }
}

// ── asyncSetProvisioned ────────────────────────────────────────────────────

/**
 * @brief Async SET_PROVISIONED: set provisioned state on responder.
 *
 * Sends through the SPDM TCP transport layer (adds TCP binding header) and
 * receives the ACK through the same path.  The responder dispatches the
 * decoded payload via its libspdm_get_response_func callback.
 *
 * @param spdmContext  Initialised libspdm requester context.
 */
inline boost::asio::awaitable<bool>
    asyncSetProvisioned(bool provisioned, void* spdmContext, AsyncSpdmTcpIO& io)
{
    auto request =
        spdm_serialization::createSetProvisionedRequest(0x12, provisioned);

    if (!co_await customSend(spdmContext, io, request))
        co_return false;

    std::vector<uint8_t> response;
    if (!co_await customReceive(spdmContext, io, response,
                                sizeof(SpdmSetProvisionedResponse)))
        co_return false;

    try
    {
        SpdmSetProvisionedResponse resp =
            spdm_serialization::deserializeSetProvisionedResponse(response);
        co_return resp.status == CERT_EXCHANGE_SUCCESS || resp.status == 1;
    }
    catch (...)
    {
        co_return false;
    }
}
