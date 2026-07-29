#pragma once
/**
 * Async port of libspdm_get_capabilities() / libspdm_try_get_capabilities().
 */

#include "libspdm_async_io.hpp"
#include "libspdm_async_utils.hpp"
#include "libspdm_req_send_receive_async.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/this_coro.hpp>

#include <bit>
#include <chrono>
#include <cstdint>
#include <utility>

extern "C"
{
#include <internal/libspdm_common_lib.h>
#include <internal/libspdm_requester_lib.h>
#include <library/spdm_common_lib.h>
}

/// SPDM capabilities_flag bit-field layout (DMTF DSP0274, little-endian).
/// Each field name matches the SPDM specification term.
struct SpdmCapabilityFlags
{
    uint32_t reserved0              : 1;  // bit  0  (reserved)
    uint32_t cert_cap               : 1;  // bit  1
    uint32_t chal_cap               : 1;  // bit  2
    uint32_t meas_cap               : 2;  // bits 3-4
    uint32_t meas_fresh_cap         : 1;  // bit  5
    uint32_t encrypt_cap            : 1;  // bit  6
    uint32_t mac_cap                : 1;  // bit  7
    uint32_t mut_auth_cap           : 1;  // bit  8
    uint32_t key_ex_cap             : 1;  // bit  9
    uint32_t psk_cap                : 2;  // bits 10-11
    uint32_t encap_cap              : 1;  // bit  12
    uint32_t hbeat_cap              : 1;  // bit  13
    uint32_t key_upd_cap            : 1;  // bit  14
    uint32_t handshake_in_the_clear_cap : 1; // bit 15
    uint32_t pub_key_id_cap         : 1;  // bit  16
    uint32_t reserved17             : 1;  // bit  17 (reserved)
    uint32_t alias_cert_cap         : 1;  // bit  18
    uint32_t set_cert_cap           : 1;  // bit  19
    uint32_t csr_cap                : 1;  // bit  20
    uint32_t cert_install_reset_cap : 1;  // bit  21
    uint32_t ep_info_cap            : 2;  // bits 22-23
    uint32_t reserved24             : 1;  // bit  24 (reserved)
    uint32_t event_cap              : 1;  // bit  25
    uint32_t multi_key_cap          : 2;  // bits 26-27
    uint32_t get_key_pair_info_cap  : 1;  // bit  28
    uint32_t set_key_pair_info_cap  : 1;  // bit  29
    uint32_t set_key_pair_reset_cap : 1;  // bit  30
    uint32_t reserved31             : 1;  // bit  31 (reserved)
};
static_assert(sizeof(SpdmCapabilityFlags) == sizeof(uint32_t),
              "SpdmCapabilityFlags must be exactly 32 bits");

namespace detail
{

// ---------------------------------------------------------------------------
// Each helper validates one cohesive rule group from DSP0274.
// They take the decoded caps struct plus the negotiated version so that the
// top-level function reads as a straight sequence of named checks.
// ---------------------------------------------------------------------------

/// Universal invariants that apply regardless of version.
/// meas_cap == 3 is reserved; meas_fresh_cap requires meas_cap to be set.
[[nodiscard]] inline bool
    check_meas_caps_universal(const SpdmCapabilityFlags& caps)
{
    if (caps.meas_cap == 3)
        return false;
    if ((caps.meas_cap == 0) && (caps.meas_fresh_cap == 1))
        return false;
    return true;
}

/// Version 1.0 rules: cert/chal symmetry and meas-cap dependency.
/// Returns true when the caps are valid for v1.0.
[[nodiscard]] inline bool
    check_caps_v10(const SpdmCapabilityFlags& caps)
{
    if ((caps.meas_cap == 0) || (caps.meas_cap == 1))
    {
        // cert and chal must be offered together or not at all.
        if (caps.cert_cap != caps.chal_cap)
            return false;
    }
    else if (caps.meas_cap == 2)
    {
        // Measurements-with-signature requires a certificate.
        if (caps.cert_cap == 0)
            return false;
    }
    return true;
}

/// Session-layer rules introduced in v1.1.
/// Validates PSK, key-exchange, MAC/encrypt, and HITC coherence.
[[nodiscard]] inline bool
    check_session_caps_v11(const SpdmCapabilityFlags& caps, uint8_t version)
{
    if (caps.psk_cap == 3) // reserved value
        return false;

    const bool session_capable = (caps.key_ex_cap == 1) || (caps.psk_cap != 0);
    if (session_capable)
    {
        // At least one of MAC or encryption must be enabled for sessions.
        if ((caps.mac_cap == 0) && (caps.encrypt_cap == 0))
            return false;
    }
    else
    {
        // No session capability: session-only features must be absent.
        if ((caps.mac_cap == 1) || (caps.encrypt_cap == 1) ||
            (caps.handshake_in_the_clear_cap == 1) ||
            (caps.hbeat_cap == 1) || (caps.key_upd_cap == 1))
            return false;
        // v1.3+ adds event_cap to the session-only set.
        if ((version >= SPDM_MESSAGE_VERSION_13) && (caps.event_cap == 1))
            return false;
    }

    // PSK-only (no key exchange): handshake-in-the-clear is forbidden.
    if ((caps.key_ex_cap == 0) && (caps.psk_cap != 0) &&
        (caps.handshake_in_the_clear_cap == 1))
        return false;

    return true;
}

/// Identity-source rules introduced in v1.1.
/// cert_cap and pub_key_id_cap are mutually exclusive; whichever is set must
/// be paired with at least one usable authentication mechanism.
[[nodiscard]] inline bool
    check_identity_caps_v11(const SpdmCapabilityFlags& caps, uint8_t version)
{
    const bool has_identity =
        (caps.cert_cap == 1) || (caps.pub_key_id_cap == 1);

    if (has_identity)
    {
        // Cannot advertise both identity sources simultaneously.
        if ((caps.cert_cap == 1) && (caps.pub_key_id_cap == 1))
            return false;

        // Must have at least one mechanism that can use the identity.
        const bool no_auth_mechanism =
            (caps.chal_cap == 0) && (caps.key_ex_cap == 0) &&
            ((caps.meas_cap == 0) || (caps.meas_cap == 1));
        if (no_auth_mechanism)
        {
            // v1.3+ allows ep_info_cap == 2 to satisfy the requirement.
            if (version >= SPDM_MESSAGE_VERSION_13)
            {
                if ((caps.ep_info_cap == 0) || (caps.ep_info_cap == 1))
                    return false;
            }
            else
                return false;
        }
    }
    else
    {
        // No identity source: features that require one must be absent.
        if ((caps.chal_cap == 1) || (caps.key_ex_cap == 1) ||
            (caps.meas_cap == 2) || (caps.mut_auth_cap == 1))
            return false;
        if ((version >= SPDM_MESSAGE_VERSION_13) && (caps.ep_info_cap == 2))
            return false;
    }

    return true;
}

/// Mutual-authentication rules introduced in v1.1.
/// mut_auth requires at least key exchange or challenge to be present.
/// In v1.1 exactly, encap_cap must also be set.
[[nodiscard]] inline bool
    check_mut_auth_caps_v11(const SpdmCapabilityFlags& caps, uint8_t version)
{
    if (caps.mut_auth_cap == 1)
    {
        if ((caps.key_ex_cap == 0) && (caps.chal_cap == 0))
            return false;
        if ((version == SPDM_MESSAGE_VERSION_11) && (caps.encap_cap == 0))
            return false;
    }
    return true;
}

/// Certificate-management rules introduced in v1.2.
/// alias_cert/set_cert require cert_cap; csr requires set_cert;
/// cert_install_reset requires csr or set_cert.
[[nodiscard]] inline bool
    check_cert_management_caps_v12(const SpdmCapabilityFlags& caps)
{
    if ((caps.cert_cap == 0) &&
        ((caps.alias_cert_cap == 1) || (caps.set_cert_cap == 1)))
        return false;
    if ((caps.csr_cap == 1) && (caps.set_cert_cap == 0))
        return false;
    if ((caps.cert_install_reset_cap == 1) &&
        (caps.csr_cap == 0) && (caps.set_cert_cap == 0))
        return false;
    return true;
}

/// Multi-key and endpoint-info rules introduced in v1.3.
/// Reserved enum values (3) are rejected; multi_key requires cert and
/// get_key_pair_info; pub_key_id is incompatible with multi-key machinery.
[[nodiscard]] inline bool
    check_multi_key_caps_v13(const SpdmCapabilityFlags& caps)
{
    if ((caps.ep_info_cap == 3) || (caps.multi_key_cap == 3))
        return false;
    if ((caps.multi_key_cap != 0) &&
        ((caps.get_key_pair_info_cap == 0) || (caps.cert_cap == 0)))
        return false;
    if ((caps.pub_key_id_cap == 1) &&
        ((caps.multi_key_cap != 0) || (caps.get_key_pair_info_cap == 1) ||
         (caps.set_key_pair_info_cap == 1)))
        return false;
    return true;
}

/// Key-pair reset rule introduced in v1.4.
/// set_key_pair_reset requires set_key_pair_info to be present.
[[nodiscard]] inline bool
    check_key_pair_reset_cap_v14(const SpdmCapabilityFlags& caps)
{
    if ((caps.set_key_pair_reset_cap == 1) && (caps.set_key_pair_info_cap == 0))
        return false;
    return true;
}

// ---------------------------------------------------------------------------

[[nodiscard]] inline bool
    validate_responder_capability_async(uint32_t capabilities_flag,
                                        uint8_t version)
{
    const auto caps = std::bit_cast<SpdmCapabilityFlags>(capabilities_flag);

    if (!check_meas_caps_universal(caps))
        return false;

    if (version == SPDM_MESSAGE_VERSION_10)
        return check_caps_v10(caps);

    // version >= 1.1 (v1.0 handled above)
    if (!check_session_caps_v11(caps, version))
        return false;
    if (!check_identity_caps_v11(caps, version))
        return false;
    if (!check_mut_auth_caps_v11(caps, version))
        return false;

    if (version >= SPDM_MESSAGE_VERSION_12)
        if (!check_cert_management_caps_v12(caps))
            return false;

    if (version >= SPDM_MESSAGE_VERSION_13)
        if (!check_multi_key_caps_v13(caps))
            return false;

    if (version >= SPDM_MESSAGE_VERSION_14)
        if (!check_key_pair_reset_cap_v14(caps))
            return false;

    return true;
}

// ---------------------------------------------------------------------------
// Compile-time sizes for GET_CAPABILITIES / CAPABILITIES before SPDM 1.2.
// SPDM 1.1 carries ct_exponent and flags but not the two v1.2 transfer-size
// fields, so request and response normalization must use their own source
// structs.
// ---------------------------------------------------------------------------
inline constexpr size_t k_get_capabilities_request_size_pre_v12 =
    sizeof(spdm_get_capabilities_request_t) -
    sizeof(spdm_get_capabilities_request_t::data_transfer_size) -
    sizeof(spdm_get_capabilities_request_t::max_spdm_msg_size);

inline constexpr size_t k_capabilities_response_size_pre_v12 =
    sizeof(spdm_capabilities_response_t) -
    sizeof(spdm_capabilities_response_t::data_transfer_size) -
    sizeof(spdm_capabilities_response_t::max_spdm_msg_size);

template <AsyncSpdmIO IO>
boost::asio::awaitable<libspdm_return_t>
    libspdm_try_get_capabilities_async(libspdm_context_t* context,
                                       size_t* supported_algs_length,
                                       void* supported_algs, IO& io)
{
    // -----------------------------------------------------------------------
    // Pre-condition: must have completed the version negotiation step.
    // -----------------------------------------------------------------------
    if (context->connection_info.connection_state !=
        LIBSPDM_CONNECTION_STATE_AFTER_VERSION)
        co_return LIBSPDM_STATUS_INVALID_STATE_LOCAL;

    libspdm_reset_message_buffer_via_request_code(context, nullptr,
                                                  SPDM_GET_CAPABILITIES);

    // -----------------------------------------------------------------------
    // Phase 1: Build and send the GET_CAPABILITIES request.
    // The sender buffer is held only for the duration of this phase.  The
    // inner block scope ensures the scope_exit guard destructs — releasing
    // the sender buffer — before Phase 2 acquires the receiver buffer.
    // -----------------------------------------------------------------------
    const size_t transport_header_size =
        context->local_context.capability.transport_header_size;

    // req_version and spdm_request_size are needed in Phase 2 for the
    // message-A transcript, so they are declared outside the inner block.
    uint8_t req_version     = 0;
    size_t  spdm_request_size = 0;

    // After Phase 1 the request lives in context->last_spdm_request.
    const spdm_get_capabilities_request_t* spdm_request = nullptr;

    {
        uint8_t* message = nullptr;
        size_t   message_size = 0;

        if (libspdm_return_t st = libspdm_acquire_sender_buffer(
                context, &message_size, as_void_pp(&message));
            LIBSPDM_STATUS_IS_ERROR(st))
            co_return st;

        // Sender buffer released unconditionally when this block exits.
        ScopeExit release_sender([&] {
            libspdm_release_sender_buffer(context);
        });

        LIBSPDM_ASSERT(message_size >= transport_header_size +
                                           context->local_context.capability
                                               .transport_tail_size);

        auto* req = reinterpret_cast<spdm_get_capabilities_request_t*>(
            message + transport_header_size);
        spdm_request_size =
            message_size - transport_header_size -
            context->local_context.capability.transport_tail_size;

        // --- Populate the request header and fields ------------------------

        LIBSPDM_ASSERT(spdm_request_size >= sizeof(req->header));
        libspdm_zero_mem(req, spdm_request_size);

        req_version = libspdm_get_connection_version(context);
        req->header.spdm_version = req_version;

        // Size depends on version: v1.2+ carries transfer-size fields; v1.1
        // carries ct_exponent/flags but not those fields; v1.0 is header-only.
        if (req_version >= SPDM_MESSAGE_VERSION_12)
            spdm_request_size = sizeof(spdm_get_capabilities_request_t);
        else if (req_version >= SPDM_MESSAGE_VERSION_11)
            spdm_request_size = k_get_capabilities_request_size_pre_v12;
        else
            spdm_request_size = sizeof(req->header);

        req->header.request_response_code = SPDM_GET_CAPABILITIES;
        req->header.param1 = 0;
        req->header.param2 = 0;

        if (req_version >= SPDM_MESSAGE_VERSION_11)
        {
            req->ct_exponent = context->local_context.capability.ct_exponent;
            req->flags = libspdm_mask_capability_flags(
                context, true, context->local_context.capability.flags);
        }
        if (supported_algs != nullptr)
        {
            LIBSPDM_ASSERT((req_version >= SPDM_MESSAGE_VERSION_13) &&
                           ((req->flags &
                             SPDM_GET_CAPABILITIES_REQUEST_FLAGS_CHUNK_CAP) != 0));
            req->header.param1 |=
                SPDM_GET_CAPABILITIES_REQUEST_PARAM1_SUPPORTED_ALGORITHMS;
        }
        if (req_version >= SPDM_MESSAGE_VERSION_12)
        {
            req->data_transfer_size =
                context->local_context.capability.data_transfer_size;
            req->max_spdm_msg_size =
                context->local_context.capability.max_spdm_msg_size;
        }
        req->ext_flags =
            (req_version >= SPDM_MESSAGE_VERSION_14)
                ? libspdm_mask_capability_ext_flags(
                      context, true,
                      context->local_context.capability.ext_flags)
                : 0;

        // --- Send ----------------------------------------------------------
        if (libspdm_return_t st = co_await libspdm_send_request_async(
                context, nullptr, false, spdm_request_size, req, io);
            LIBSPDM_STATUS_IS_ERROR(st))
            co_return st; // release_sender fires here via scope_exit

        // release_sender destructs here — sender buffer freed.
    }

    // After libspdm_release_sender_buffer, the library archives the sent
    // request into context->last_spdm_request for transcript use.
    spdm_request = reinterpret_cast<const spdm_get_capabilities_request_t*>(
        context->last_spdm_request);

    // -----------------------------------------------------------------------
    // Phase 2: Receive and validate the CAPABILITIES response.
    // The receiver buffer is held for the duration of this phase and is
    // released unconditionally by a scope guard at function exit.
    // -----------------------------------------------------------------------
    uint8_t* message = nullptr;
    size_t   message_size = 0;

    if (libspdm_return_t st = libspdm_acquire_receiver_buffer(
            context, &message_size, as_void_pp(&message));
        LIBSPDM_STATUS_IS_ERROR(st))
        co_return st;

    auto* spdm_response =
        reinterpret_cast<spdm_capabilities_response_t*>(message);
    size_t spdm_response_size = message_size;

    // Receiver buffer released on every exit path from this point on.
    libspdm_return_t status = LIBSPDM_STATUS_SUCCESS;
    ScopeExit release_receiver([&] {
        libspdm_release_receiver_buffer(context);
    });

    LIBSPDM_ASSERT(message_size >= transport_header_size);

    status = co_await libspdm_receive_response_async(
        context, nullptr, false, &spdm_response_size,
        as_void_pp(&spdm_response), io);
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    // --- Validate the response header --------------------------------------

    if (spdm_response_size < sizeof(spdm_message_header_t))
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    if (spdm_response->header.request_response_code == SPDM_ERROR)
    {
        // libspdm_handle_simple_error_response returns an error status on
        // unrecoverable errors and LIBSPDM_STATUS_SUCCESS on a Busy reply.
        status = libspdm_handle_simple_error_response(
            context, spdm_response->header.param1);
        if (LIBSPDM_STATUS_IS_ERROR(status))
            co_return status;
    }
    else if (spdm_response->header.request_response_code != SPDM_CAPABILITIES)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    const uint8_t resp_version = spdm_response->header.spdm_version;
    if (resp_version != req_version)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    // --- Normalise and validate the response size --------------------------

    if ((resp_version >= SPDM_MESSAGE_VERSION_13) &&
        (spdm_request->header.param1 &
         SPDM_GET_CAPABILITIES_REQUEST_PARAM1_SUPPORTED_ALGORITHMS) &&
        (spdm_response->header.param1 &
         SPDM_CAPABILITIES_RESPONSE_PARAM1_SUPPORTED_ALGORITHMS))
    {
        if (spdm_response_size < sizeof(spdm_capabilities_response_t) +
                                     sizeof(spdm_supported_algorithms_block_t))
            co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

        auto* supported_algorithms =
            reinterpret_cast<spdm_supported_algorithms_block_t*>(
                reinterpret_cast<uint8_t*>(spdm_response) +
                sizeof(spdm_capabilities_response_t));

        if (supported_algorithms->length <
            sizeof(spdm_supported_algorithms_block_t))
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
        if (supported_algorithms->length >
            spdm_response_size - sizeof(spdm_capabilities_response_t))
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

        const uint32_t expected_block_length =
            static_cast<uint32_t>(sizeof(spdm_supported_algorithms_block_t)) +
            static_cast<uint32_t>(supported_algorithms->ext_asym_count) *
                sizeof(spdm_extended_algorithm_t) +
            static_cast<uint32_t>(supported_algorithms->ext_hash_count) *
                sizeof(spdm_extended_algorithm_t) +
            static_cast<uint32_t>(supported_algorithms->param1) *
                sizeof(spdm_negotiate_algorithms_common_struct_table_t);
        if (supported_algorithms->length != expected_block_length)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

        spdm_response_size = sizeof(spdm_capabilities_response_t) +
                             supported_algorithms->length;
    }
    else if ((resp_version >= SPDM_MESSAGE_VERSION_13) &&
             (spdm_request->header.param1 &
              SPDM_GET_CAPABILITIES_REQUEST_PARAM1_SUPPORTED_ALGORITHMS) &&
             !(spdm_response->header.param1 &
               SPDM_CAPABILITIES_RESPONSE_PARAM1_SUPPORTED_ALGORITHMS))
    {
        spdm_response_size = sizeof(spdm_capabilities_response_t);
    }
    else if (resp_version >= SPDM_MESSAGE_VERSION_12)
    {
        if (spdm_response_size < sizeof(spdm_capabilities_response_t))
            co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;
        spdm_response_size = sizeof(spdm_capabilities_response_t);
    }
    else
    {
        if (spdm_response_size < k_capabilities_response_size_pre_v12)
            co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;
        spdm_response_size = k_capabilities_response_size_pre_v12;
    }

    // --- Validate capability flags ----------------------------------------

    if (!validate_responder_capability_async(spdm_response->flags, resp_version))
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    if (resp_version >= SPDM_MESSAGE_VERSION_12)
    {
        // data_transfer_size must be in [SPDM_MIN, max_spdm_msg_size].
        if ((spdm_response->data_transfer_size <
             SPDM_MIN_DATA_TRANSFER_SIZE_VERSION_12) ||
            (spdm_response->data_transfer_size >
             spdm_response->max_spdm_msg_size))
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

        // Without CHUNK_CAP the two sizes must be equal.
        const bool has_chunk_cap =
            (spdm_response->flags &
             SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CHUNK_CAP) != 0;
        if (!has_chunk_cap &&
            (spdm_response->data_transfer_size != spdm_response->max_spdm_msg_size))
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
    }

    if (spdm_response->ct_exponent > LIBSPDM_MAX_CT_EXPONENT)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    // --- Append to transcript (message A) ----------------------------------

    status = libspdm_append_message_a(context, spdm_request, spdm_request_size);
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    status = libspdm_append_message_a(context, spdm_response, spdm_response_size);
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    // --- Commit negotiated capabilities to the context ---------------------

    auto& peer = context->connection_info.capability;
    peer.ct_exponent = spdm_response->ct_exponent;
    peer.flags = libspdm_mask_capability_flags(context, false, spdm_response->flags);
    peer.ext_flags =
        (resp_version >= SPDM_MESSAGE_VERSION_14)
            ? libspdm_mask_capability_ext_flags(context, false,
                                                spdm_response->ext_flags)
            : 0;

    if (resp_version >= SPDM_MESSAGE_VERSION_12)
    {
        peer.data_transfer_size = spdm_response->data_transfer_size;
        peer.max_spdm_msg_size  = spdm_response->max_spdm_msg_size;
    }
    else
    {
        peer.data_transfer_size = 0;
        peer.max_spdm_msg_size  = 0;
    }

    if ((supported_algs != nullptr) && (supported_algs_length != nullptr) &&
        (resp_version >= SPDM_MESSAGE_VERSION_13) &&
        (spdm_request->header.param1 &
         SPDM_GET_CAPABILITIES_REQUEST_PARAM1_SUPPORTED_ALGORITHMS) &&
        (spdm_response->header.param1 &
         SPDM_CAPABILITIES_RESPONSE_PARAM1_SUPPORTED_ALGORITHMS))
    {
        auto* supported_algorithms =
            reinterpret_cast<spdm_supported_algorithms_block_t*>(
                reinterpret_cast<uint8_t*>(spdm_response) +
                sizeof(spdm_capabilities_response_t));
        const size_t algorithm_data_size =
            spdm_response_size - sizeof(spdm_capabilities_response_t);

        if (*supported_algs_length >= algorithm_data_size)
        {
            libspdm_copy_mem(supported_algs, *supported_algs_length,
                             supported_algorithms, algorithm_data_size);
            *supported_algs_length = algorithm_data_size;
        }
        else
        {
            *supported_algs_length = algorithm_data_size;
            co_return LIBSPDM_STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (supported_algs_length != nullptr)
    {
        *supported_algs_length = 0;
    }

    context->connection_info.connection_state =
        LIBSPDM_CONNECTION_STATE_AFTER_CAPABILITIES;

#if LIBSPDM_ENABLE_MSG_LOG
    libspdm_append_msg_log(context, spdm_response, spdm_response_size);
#endif

    co_return LIBSPDM_STATUS_SUCCESS;
    // release_receiver destructs here (and on every co_return above).
}

} // namespace detail

template <AsyncSpdmIO IO>
boost::asio::awaitable<libspdm_return_t>
    libspdm_get_capabilities_async(void* spdm_context, IO& io)
{
    auto* context = static_cast<libspdm_context_t*>(spdm_context);
    context->crypto_request = false;

    size_t retry = context->retry_times;
    uint64_t retry_delay_us = context->retry_delay_time;
    libspdm_return_t status;

    do
    {
        status = co_await detail::libspdm_try_get_capabilities_async(
            context, nullptr, nullptr, io);
        if (status != LIBSPDM_STATUS_BUSY_PEER)
            co_return status;

        if (retry != 0 && retry_delay_us > 0)
        {
            auto executor = co_await boost::asio::this_coro::executor;
            boost::asio::steady_timer timer(executor);
            timer.expires_after(std::chrono::microseconds(retry_delay_us));
            co_await timer.async_wait(boost::asio::use_awaitable);
        }
    } while (retry-- != 0);

    co_return status;
}

template <AsyncSpdmIO IO>
boost::asio::awaitable<libspdm_return_t>
    libspdm_get_capabilities_with_supported_algs_async(
        void* spdm_context, size_t* supported_algs_length,
        void* supported_algs, IO& io)
{
    auto* context = static_cast<libspdm_context_t*>(spdm_context);
    LIBSPDM_ASSERT((supported_algs == nullptr) ||
                   (supported_algs_length != nullptr));
    context->crypto_request = false;

    size_t retry = context->retry_times;
    uint64_t retry_delay_us = context->retry_delay_time;
    libspdm_return_t status;

    do
    {
        status = co_await detail::libspdm_try_get_capabilities_async(
            context, supported_algs_length, supported_algs, io);
        if (status != LIBSPDM_STATUS_BUSY_PEER)
            co_return status;

        if (retry != 0 && retry_delay_us > 0)
        {
            auto executor = co_await boost::asio::this_coro::executor;
            boost::asio::steady_timer timer(executor);
            timer.expires_after(std::chrono::microseconds(retry_delay_us));
            co_await timer.async_wait(boost::asio::use_awaitable);
        }
    } while (retry-- != 0);

    co_return status;
}
