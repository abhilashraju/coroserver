#pragma once
/**
 * Async port of libspdm_get_measurement().
 *
 * Sends GET_MEASUREMENTS and receives MEASUREMENTS entirely via co_await.
 * The request layout, response validation, transcript (message M) handling
 * and signature verification follow libspdm_try_get_measurement() in
 * library/spdm_requester_lib/libspdm_req_get_measurements.c.
 *
 * Returns the raw measurement record bytes which callers can encode to base64.
 * When a signature is requested the peer certificate chain of `slot_id` must
 * have been fetched first (see libspdm_get_certificate_async()).
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

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

/**
 * @brief Optional extra outputs of libspdm_get_measurement_async().
 */
struct AsyncMeasurementExtras
{
    /// Nonce sent to the responder (all zero when no signature requested).
    std::array<uint8_t, SPDM_NONCE_SIZE> requesterNonce{};
    /// Nonce returned by the responder.
    std::array<uint8_t, SPDM_NONCE_SIZE> responderNonce{};
    /// Responder opaque data.
    std::vector<uint8_t> opaqueData;
    /// The GET_MEASUREMENTS request exactly as sent.
    std::vector<uint8_t> rawRequest;
    /// The complete MEASUREMENTS response, including the signature if signed.
    std::vector<uint8_t> rawResponse;
    /// The signature carried by the response (empty when unsigned).
    std::vector<uint8_t> signature;
    /// Negotiated SPDM version number used on the wire (e.g. 0x12 for 1.2).
    uint8_t version = 0;
    /// content_changed field of a signed response (SPDM 1.2+).
    uint8_t contentChanged = 0;
};

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
 * @param nonce_in            Optional 32 byte requester nonce. A random nonce
 *                            is generated when null.
 * @param extras              Optional additional outputs.
 */
template <AsyncSpdmIO IO>
boost::asio::awaitable<libspdm_return_t> libspdm_get_measurement_async(
    void* spdm_context, const uint32_t* session_id, uint8_t request_attribute,
    uint8_t measurement_operation, uint8_t slot_id, uint8_t* number_of_blocks,
    std::vector<uint8_t>& measurements_out, IO& io,
    const uint8_t* nonce_in = nullptr, AsyncMeasurementExtras* extras = nullptr)
{
    auto* context = static_cast<libspdm_context_t*>(spdm_context);

    const size_t transport_header_size =
        context->local_context.capability.transport_header_size;

    measurements_out.clear();
    if (extras != nullptr)
    {
        *extras = AsyncMeasurementExtras{};
    }

    const bool signRequested =
        (request_attribute &
         SPDM_GET_MEASUREMENTS_REQUEST_ATTRIBUTES_GENERATE_SIGNATURE) != 0;

    // ── Verify state ───────────────────────────────────────────────────────
    if (!libspdm_is_capabilities_flag_supported(
            context, true, 0, SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEAS_CAP))
        co_return LIBSPDM_STATUS_UNSUPPORTED_CAP;

    if (context->connection_info.connection_state <
        LIBSPDM_CONNECTION_STATE_NEGOTIATED)
        co_return LIBSPDM_STATUS_INVALID_STATE_LOCAL;

    if (slot_id >= SPDM_MAX_SLOT_COUNT)
        co_return LIBSPDM_STATUS_INVALID_PARAMETER;

    if (signRequested &&
        libspdm_is_capabilities_flag_supported(
            context, true, 0,
            SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEAS_CAP_NO_SIG))
        co_return LIBSPDM_STATUS_INVALID_PARAMETER;

    libspdm_session_info_t* session_info = nullptr;
    if (session_id != nullptr)
    {
        session_info = static_cast<libspdm_session_info_t*>(
            libspdm_get_session_info_via_session_id(context, *session_id));
        if (session_info == nullptr)
            co_return LIBSPDM_STATUS_INVALID_STATE_LOCAL;
    }

    size_t signature_size = 0;
    if (signRequested)
    {
        signature_size =
            context->connection_info.algorithm.pqc_asym_algo != 0
                ? libspdm_get_pqc_asym_signature_size(
                      context->connection_info.algorithm.pqc_asym_algo)
                : libspdm_get_asym_signature_size(
                      context->connection_info.algorithm.base_asym_algo);
    }

    libspdm_reset_message_buffer_via_request_code(context, nullptr,
                                                  SPDM_GET_MEASUREMENTS);

    // ── Acquire sender buffer ──────────────────────────────────────────────
    uint8_t* message = nullptr;
    size_t message_size = 0;
    libspdm_return_t status = libspdm_acquire_sender_buffer(
        context, &message_size, detail::as_void_pp(&message));
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    LIBSPDM_ASSERT(message_size >= transport_header_size);

    auto* req = reinterpret_cast<spdm_get_measurements_request_t*>(
        message + transport_header_size);
    size_t req_size = sizeof(spdm_message_header_t);

    const uint8_t version = libspdm_get_connection_version(context);
    req->header.spdm_version = version;
    req->header.request_response_code = SPDM_GET_MEASUREMENTS;
    req->header.param1 = request_attribute;
    req->header.param2 = measurement_operation;

    std::array<uint8_t, SPDM_NONCE_SIZE> requesterNonce{};
    if (signRequested)
    {
        if (version >= SPDM_MESSAGE_VERSION_13)
            req_size = sizeof(spdm_get_measurements_request_t) +
                       SPDM_REQ_CONTEXT_SIZE;
        else if (version >= SPDM_MESSAGE_VERSION_11)
            req_size = sizeof(spdm_get_measurements_request_t);
        else
            req_size = sizeof(spdm_get_measurements_request_t) -
                       sizeof(req->slot_id_param);

        if (nonce_in == nullptr)
        {
            if (!libspdm_get_random_number(SPDM_NONCE_SIZE, req->nonce))
            {
                libspdm_release_sender_buffer(context);
                co_return LIBSPDM_STATUS_LOW_ENTROPY;
            }
        }
        else
        {
            std::memcpy(req->nonce, nonce_in, SPDM_NONCE_SIZE);
        }
        std::memcpy(requesterNonce.data(), req->nonce, SPDM_NONCE_SIZE);
        req->slot_id_param = slot_id;
    }
    else if (version >= SPDM_MESSAGE_VERSION_13)
    {
        req_size = sizeof(spdm_message_header_t) + SPDM_REQ_CONTEXT_SIZE;
    }
    if (version >= SPDM_MESSAGE_VERSION_13)
    {
        // No requester context is used.
        std::memset(reinterpret_cast<uint8_t*>(req) + req_size -
                        SPDM_REQ_CONTEXT_SIZE,
                    0, SPDM_REQ_CONTEXT_SIZE);
    }

    // ── Send ──────────────────────────────────────────────────────────────
    status = co_await libspdm_send_request_async(context, session_id, false,
                                                 req_size, req, io);
    libspdm_release_sender_buffer(context);
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    // The sent request is kept by libspdm for transcript and context checks.
    const auto* sentReq = reinterpret_cast<const uint8_t*>(
        context->last_spdm_request);

    // ── Acquire receiver buffer ────────────────────────────────────────────
    status = libspdm_acquire_receiver_buffer(context, &message_size,
                                             detail::as_void_pp(&message));
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    bool succeeded = false;
    detail::ScopeExit release_rx([context, &succeeded, session_info] {
        if (!succeeded)
            libspdm_reset_message_m(context, session_info);
        libspdm_release_receiver_buffer(context);
    });

    auto* resp = reinterpret_cast<spdm_measurements_response_t*>(message);
    size_t resp_size = message_size;

    status = co_await libspdm_receive_response_async(
        context, session_id, false, &resp_size, detail::as_void_pp(&resp), io);
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    // ── Validate response ─────────────────────────────────────────────────
    if (resp_size < sizeof(spdm_message_header_t))
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    if (resp->header.request_response_code == SPDM_ERROR)
    {
        status = libspdm_handle_simple_error_response(context,
                                                      resp->header.param1);
        co_return LIBSPDM_STATUS_IS_ERROR(status)
            ? status
            : LIBSPDM_STATUS_INVALID_MSG_FIELD;
    }
    if (resp->header.request_response_code != SPDM_MEASUREMENTS)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
    if (resp->header.spdm_version != version)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
    if (resp_size < sizeof(spdm_measurements_response_t))
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    if (measurement_operation ==
        SPDM_GET_MEASUREMENTS_REQUEST_MEASUREMENT_OPERATION_TOTAL_NUMBER_OF_MEASUREMENTS)
    {
        if (resp->number_of_blocks != 0)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
    }
    else if (measurement_operation ==
             SPDM_GET_MEASUREMENTS_REQUEST_MEASUREMENT_OPERATION_ALL_MEASUREMENTS)
    {
        if (resp->number_of_blocks == 0 || resp->number_of_blocks == 0xff)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
    }
    else if (resp->number_of_blocks != 1)
    {
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
    }

    // measurement_record_length is a 3 byte little-endian field.
    const uint32_t record_length =
        static_cast<uint32_t>(resp->measurement_record_length[0]) |
        (static_cast<uint32_t>(resp->measurement_record_length[1]) << 8) |
        (static_cast<uint32_t>(resp->measurement_record_length[2]) << 16);

    if (measurement_operation ==
        SPDM_GET_MEASUREMENTS_REQUEST_MEASUREMENT_OPERATION_TOTAL_NUMBER_OF_MEASUREMENTS)
    {
        if (record_length != 0)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
    }
    else if (resp_size < sizeof(spdm_measurements_response_t) + record_length)
    {
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;
    }

    const auto* respBytes = reinterpret_cast<const uint8_t*>(resp);
    const uint8_t* record_ptr = respBytes + sizeof(spdm_measurements_response_t);

    // nonce + opaque_length must be present
    if (resp_size < sizeof(spdm_measurements_response_t) + record_length +
                        SPDM_NONCE_SIZE + sizeof(uint16_t))
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    if (signRequested && version >= SPDM_MESSAGE_VERSION_11 &&
        (resp->header.param2 & SPDM_MEASUREMENTS_RESPONSE_SLOT_ID_MASK) !=
            slot_id)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    const uint8_t* ptr = record_ptr + record_length;
    const uint8_t* responderNonce = ptr;
    ptr += SPDM_NONCE_SIZE;

    const uint16_t opaque_length =
        static_cast<uint16_t>(ptr[0] | (ptr[1] << 8));
    if (opaque_length > SPDM_MAX_OPAQUE_DATA_SIZE)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
    if (signRequested && version >= SPDM_MESSAGE_VERSION_12 &&
        (context->connection_info.algorithm.other_params_support &
         SPDM_ALGORITHMS_OPAQUE_DATA_FORMAT_MASK) ==
            SPDM_ALGORITHMS_OPAQUE_DATA_FORMAT_NONE &&
        opaque_length != 0)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
    ptr += sizeof(uint16_t);

    const size_t fixedSize = sizeof(spdm_measurements_response_t) +
                             record_length + SPDM_NONCE_SIZE +
                             sizeof(uint16_t) + opaque_length;
    if (resp_size < fixedSize)
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    if (opaque_length != 0 && !libspdm_process_general_opaque_data_check(
                                  context, opaque_length, ptr))
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    const uint8_t* opaquePtr = ptr;
    ptr += opaque_length;

    size_t contextSize = 0;
    if (version >= SPDM_MESSAGE_VERSION_13)
    {
        contextSize = SPDM_REQ_CONTEXT_SIZE;
        if (resp_size < fixedSize + contextSize + signature_size)
            co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;
        if (!libspdm_consttime_is_mem_equal(
                sentReq + req_size - SPDM_REQ_CONTEXT_SIZE, ptr,
                SPDM_REQ_CONTEXT_SIZE))
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
        ptr += SPDM_REQ_CONTEXT_SIZE;
    }
    else if (resp_size < fixedSize + signature_size)
    {
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;
    }

    // Size of the response up to and including the signature.
    const size_t usedSize = fixedSize + contextSize + signature_size;
    const size_t unsignedSize = usedSize - signature_size;

    // ── Transcript M and signature ────────────────────────────────────────
    if (signRequested)
    {
        status = libspdm_append_message_m(context, session_info, sentReq,
                                          req_size);
        if (LIBSPDM_STATUS_IS_ERROR(status))
            co_return status;
        status = libspdm_append_message_m(context, session_info, resp,
                                          unsignedSize);
        if (LIBSPDM_STATUS_IS_ERROR(status))
            co_return status;

        if (!libspdm_verify_measurement_signature(context, session_info,
                                                  slot_id, ptr,
                                                  signature_size))
            co_return LIBSPDM_STATUS_VERIF_FAIL;

        libspdm_reset_message_m(context, session_info);
    }
    else
    {
        // content_changed must be 0 when no signature was requested.
        if (version >= SPDM_MESSAGE_VERSION_12 &&
            (resp->header.param2 & SPDM_MEASUREMENTS_RESPONSE_CONTENT_CHANGE_MASK) !=
                0)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

        status = libspdm_append_message_m(context, session_info, sentReq,
                                          req_size);
        if (LIBSPDM_STATUS_IS_ERROR(status))
            co_return status;
        status = libspdm_append_message_m(context, session_info, resp,
                                          unsignedSize);
        if (LIBSPDM_STATUS_IS_ERROR(status))
            co_return status;
    }

    // ── Outputs ───────────────────────────────────────────────────────────
    if (measurement_operation ==
        SPDM_GET_MEASUREMENTS_REQUEST_MEASUREMENT_OPERATION_TOTAL_NUMBER_OF_MEASUREMENTS)
    {
        const uint8_t count = resp->header.param1;
        if (count == 0xFF || count == 0)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
        if (number_of_blocks)
            *number_of_blocks = count;
    }
    else
    {
        const uint8_t blocks = resp->number_of_blocks;
        if (record_length < sizeof(spdm_measurement_block_common_header_t))
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

        // Validate the measurement block structure.
        uint32_t blockOffset = 0;
        uint8_t blockCount = 1;
        while (blockOffset < record_length)
        {
            if (record_length - blockOffset <
                sizeof(spdm_measurement_block_common_header_t))
                co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
            spdm_measurement_block_common_header_t header;
            std::memcpy(&header, record_ptr + blockOffset, sizeof(header));
            if (header.measurement_size >
                record_length - blockOffset -
                    sizeof(spdm_measurement_block_common_header_t))
                co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
            if (header.measurement_specification == 0 ||
                (header.measurement_specification &
                 (header.measurement_specification - 1)))
                co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
            if (header.measurement_specification !=
                context->connection_info.algorithm.measurement_spec)
                co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
            if (header.index == 0 || header.index == 0xFF)
                co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
            if (measurement_operation !=
                    SPDM_GET_MEASUREMENTS_REQUEST_MEASUREMENT_OPERATION_ALL_MEASUREMENTS &&
                header.index != measurement_operation)
                co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
            if (blockCount > blocks)
                co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
            blockCount++;
            blockOffset += static_cast<uint32_t>(
                sizeof(spdm_measurement_block_common_header_t) +
                header.measurement_size);
        }
        if (blockOffset != record_length)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

        if (number_of_blocks)
            *number_of_blocks = blocks;
        measurements_out.assign(record_ptr, record_ptr + record_length);
    }

    if (extras != nullptr)
    {
        extras->requesterNonce = requesterNonce;
        std::memcpy(extras->responderNonce.data(), responderNonce,
                    SPDM_NONCE_SIZE);
        extras->opaqueData.assign(opaquePtr, opaquePtr + opaque_length);
        extras->rawRequest.assign(sentReq, sentReq + req_size);
        extras->rawResponse.assign(respBytes, respBytes + usedSize);
        if (signRequested)
        {
            extras->signature.assign(ptr, ptr + signature_size);
            if (version >= SPDM_MESSAGE_VERSION_12)
            {
                extras->contentChanged =
                    resp->header.param2 &
                    SPDM_MEASUREMENTS_RESPONSE_CONTENT_CHANGE_MASK;
            }
        }
        extras->version = version;
    }

    succeeded = true;
    co_return LIBSPDM_STATUS_SUCCESS;
}
