#pragma once
/**
 * Async port of libspdm_negotiate_algorithms() / libspdm_try_negotiate_algorithms().
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

// ---------------------------------------------------------------------------
// alg_count byte encoding: high nibble = fixed_size_words (2), low nibble =
// ext_count.  All entries sent by the requester use "2 fixed, 0 ext" = 0x20.
// ---------------------------------------------------------------------------
static constexpr uint8_t k_alg_count_fixed2_ext0 = 0x20u;

// ---------------------------------------------------------------------------
// Capability predicates — each collapses a multi-flag OR-chain to a single
// named test, following the same pattern as libspdm_req_get_capabilities_async.
// ---------------------------------------------------------------------------

/// True when the negotiated session requires a valid base-hash algorithm.
[[nodiscard]] inline bool
    needs_base_hash(libspdm_context_t* ctx)
{
    return libspdm_is_capabilities_flag_supported(
               ctx, true, 0,
               SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CERT_CAP) ||
           libspdm_is_capabilities_flag_supported(
               ctx, true, 0,
               SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CHAL_CAP) ||
           libspdm_is_capabilities_flag_supported(
               ctx, true, 0,
               SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEAS_CAP_SIG) ||
           libspdm_is_capabilities_flag_supported(
               ctx, true,
               SPDM_GET_CAPABILITIES_REQUEST_FLAGS_KEY_EX_CAP,
               SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_KEY_EX_CAP) ||
           libspdm_is_capabilities_flag_supported(
               ctx, true,
               SPDM_GET_CAPABILITIES_REQUEST_FLAGS_PSK_CAP,
               SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PSK_CAP);
}

/// True when the negotiated session requires a valid asymmetric algorithm
/// (used for certificate / challenge / measurement-with-signature / key-ex).
[[nodiscard]] inline bool
    needs_asym_algo(libspdm_context_t* ctx)
{
    return libspdm_is_capabilities_flag_supported(
               ctx, true, 0,
               SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CERT_CAP) ||
           libspdm_is_capabilities_flag_supported(
               ctx, true, 0,
               SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CHAL_CAP) ||
           libspdm_is_capabilities_flag_supported(
               ctx, true, 0,
               SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEAS_CAP_SIG) ||
           libspdm_is_capabilities_flag_supported(
               ctx, true,
               SPDM_GET_CAPABILITIES_REQUEST_FLAGS_KEY_EX_CAP,
               SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_KEY_EX_CAP);
}

/// True when the negotiated session requires DHE or KEM (key-exchange cap).
[[nodiscard]] inline bool
    needs_key_exchange(libspdm_context_t* ctx)
{
    return libspdm_is_capabilities_flag_supported(
        ctx, true,
        SPDM_GET_CAPABILITIES_REQUEST_FLAGS_KEY_EX_CAP,
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_KEY_EX_CAP);
}

/// True when the negotiated session requires AEAD (encrypt or MAC cap).
[[nodiscard]] inline bool
    needs_aead(libspdm_context_t* ctx)
{
    return libspdm_is_capabilities_flag_supported(
               ctx, true,
               SPDM_GET_CAPABILITIES_REQUEST_FLAGS_ENCRYPT_CAP,
               SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_ENCRYPT_CAP) ||
           libspdm_is_capabilities_flag_supported(
               ctx, true,
               SPDM_GET_CAPABILITIES_REQUEST_FLAGS_MAC_CAP,
               SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MAC_CAP);
}

/// True when the negotiated session requires mutual authentication.
[[nodiscard]] inline bool
    needs_mut_auth(libspdm_context_t* ctx)
{
    return libspdm_is_capabilities_flag_supported(
               ctx, true,
               SPDM_GET_CAPABILITIES_REQUEST_FLAGS_MUT_AUTH_CAP,
               SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MUT_AUTH_CAP) ||
           libspdm_is_capabilities_flag_supported(
               ctx, true,
               SPDM_GET_CAPABILITIES_REQUEST_FLAGS_EP_INFO_CAP_SIG, 0);
}

/// True when the negotiated session requires a key-schedule (key-ex or PSK).
[[nodiscard]] inline bool
    needs_key_schedule(libspdm_context_t* ctx)
{
    return libspdm_is_capabilities_flag_supported(
               ctx, true,
               SPDM_GET_CAPABILITIES_REQUEST_FLAGS_KEY_EX_CAP,
               SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_KEY_EX_CAP) ||
           libspdm_is_capabilities_flag_supported(
               ctx, true,
               SPDM_GET_CAPABILITIES_REQUEST_FLAGS_PSK_CAP,
               SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PSK_CAP);
}

// ---------------------------------------------------------------------------

#pragma pack(1)
struct libspdm_negotiate_algorithms_request_async_t
{
    spdm_message_header_t header;
    uint16_t length;
    uint8_t measurement_specification;
    uint8_t other_params_support;
    uint32_t base_asym_algo;
    uint32_t base_hash_algo;
    uint32_t pqc_asym_algo;
    uint8_t reserved2[8];
    uint8_t ext_asym_count;
    uint8_t ext_hash_count;
    uint8_t reserved3;
    uint8_t mel_specification;
    spdm_negotiate_algorithms_common_struct_table_t
        struct_table[SPDM_NEGOTIATE_ALGORITHMS_MAX_NUM_STRUCT_TABLE_ALG_14];
};

struct libspdm_algorithms_response_async_t
{
    spdm_message_header_t header;
    uint16_t length;
    uint8_t measurement_specification_sel;
    uint8_t other_params_selection;
    uint32_t measurement_hash_algo;
    uint32_t base_asym_sel;
    uint32_t base_hash_sel;
    uint32_t pqc_asym_sel;
    uint8_t reserved2[7];
    uint8_t mel_specification_sel;
    uint8_t ext_asym_sel_count;
    uint8_t ext_hash_sel_count;
    uint16_t reserved3;
    uint32_t ext_asym_sel;
    uint32_t ext_hash_sel;
    spdm_negotiate_algorithms_common_struct_table_t
        struct_table[SPDM_NEGOTIATE_ALGORITHMS_MAX_NUM_STRUCT_TABLE_ALG_14];
};
#pragma pack()

// ---------------------------------------------------------------------------
// resp_struct_table_begin — pointer to the first struct-table entry in a
// ALGORITHMS response (after the fixed header and the ext-sel arrays).
// Used in both the validation pass and the dispatch pass so the formula
// is written in one place.
// ---------------------------------------------------------------------------
[[nodiscard]] inline const spdm_negotiate_algorithms_common_struct_table_t*
    resp_struct_table_begin(const libspdm_algorithms_response_async_t* r)
{
    const uintptr_t base =
        reinterpret_cast<uintptr_t>(r) +
        sizeof(spdm_algorithms_response_t) +
        sizeof(uint32_t) * static_cast<size_t>(r->ext_asym_sel_count) +
        sizeof(uint32_t) * static_cast<size_t>(r->ext_hash_sel_count);
    return reinterpret_cast<const spdm_negotiate_algorithms_common_struct_table_t*>(base);
}

// ---------------------------------------------------------------------------
// next_struct_table — advance to the next entry, skipping the ext-alg words.
// ---------------------------------------------------------------------------
[[nodiscard]] inline const spdm_negotiate_algorithms_common_struct_table_t*
    next_struct_table(
        const spdm_negotiate_algorithms_common_struct_table_t* t) noexcept
{
    const uint8_t ext = t->alg_count & 0xFu;
    return reinterpret_cast<const spdm_negotiate_algorithms_common_struct_table_t*>(
        reinterpret_cast<uintptr_t>(t) +
        sizeof(spdm_negotiate_algorithms_common_struct_table_t) +
        sizeof(uint32_t) * static_cast<size_t>(ext));
}

template <AsyncSpdmIO IO>
[[nodiscard]] boost::asio::awaitable<libspdm_return_t>
    libspdm_try_negotiate_algorithms_async(libspdm_context_t* context, IO& io)
{
    libspdm_return_t status;
    libspdm_negotiate_algorithms_request_async_t* spdm_request;
    size_t spdm_request_size;
    libspdm_algorithms_response_async_t* spdm_response;
    size_t spdm_response_size;
    size_t index = 0;
    uint8_t* message;
    size_t message_size;
    uint8_t req_param1 = 0;

    if (context->connection_info.connection_state !=
        LIBSPDM_CONNECTION_STATE_AFTER_CAPABILITIES)
        co_return LIBSPDM_STATUS_INVALID_STATE_LOCAL;

    libspdm_reset_message_buffer_via_request_code(context, nullptr,
                                                  SPDM_NEGOTIATE_ALGORITHMS);

    const size_t transport_header_size =
        context->local_context.capability.transport_header_size;
    status = libspdm_acquire_sender_buffer(context, &message_size,
                                           detail::as_void_pp(&message));
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    ScopeExit release_sender([&] { libspdm_release_sender_buffer(context); });

    LIBSPDM_ASSERT(message_size >= transport_header_size +
                                       context->local_context.capability
                                           .transport_tail_size);
    spdm_request =
        reinterpret_cast<libspdm_negotiate_algorithms_request_async_t*>(
            message + transport_header_size);
    spdm_request_size =
        message_size - transport_header_size -
        context->local_context.capability.transport_tail_size;

    LIBSPDM_ASSERT(spdm_request_size >=
                   sizeof(spdm_negotiate_algorithms_request_t));
    libspdm_zero_mem(spdm_request, spdm_request_size);
    spdm_request->header.spdm_version = libspdm_get_connection_version(context);

    if (spdm_request->header.spdm_version >= SPDM_MESSAGE_VERSION_11)
    {
        if (context->local_context.algorithm.dhe_named_group != 0)
            req_param1++;
        if (context->local_context.algorithm.aead_cipher_suite != 0)
            req_param1++;
        if (context->local_context.algorithm.req_base_asym_alg != 0)
            req_param1++;
        if (context->local_context.algorithm.key_schedule != 0)
            req_param1++;
        LIBSPDM_ASSERT(
            req_param1 <= SPDM_NEGOTIATE_ALGORITHMS_MAX_NUM_STRUCT_TABLE_ALG);
        if (spdm_request->header.spdm_version >= SPDM_MESSAGE_VERSION_14)
        {
            if (context->local_context.algorithm.req_pqc_asym_alg != 0)
                req_param1++;
            if (context->local_context.algorithm.kem_alg != 0)
                req_param1++;
            LIBSPDM_ASSERT(req_param1 <=
                           SPDM_NEGOTIATE_ALGORITHMS_MAX_NUM_STRUCT_TABLE_ALG_14);
        }
        spdm_request->header.param1 = req_param1;
        spdm_request->length = static_cast<uint16_t>(
            offsetof(libspdm_negotiate_algorithms_request_async_t, struct_table) +
            static_cast<size_t>(req_param1) *
                sizeof(spdm_negotiate_algorithms_common_struct_table_t));
    }
    else
    {
        spdm_request->length = static_cast<uint16_t>(
            offsetof(libspdm_negotiate_algorithms_request_async_t, struct_table));
        spdm_request->header.param1 = 0;
    }

    LIBSPDM_ASSERT(spdm_request_size >= spdm_request->length);
    spdm_request->header.request_response_code = SPDM_NEGOTIATE_ALGORITHMS;
    spdm_request->header.param2 = 0;
    spdm_request->measurement_specification =
        context->local_context.algorithm.measurement_spec;

    if (spdm_request->header.spdm_version >= SPDM_MESSAGE_VERSION_12)
    {
        spdm_request->other_params_support =
            context->local_context.algorithm.other_params_support &
            SPDM_ALGORITHMS_OPAQUE_DATA_FORMAT_MASK;
        if (spdm_request->header.spdm_version >= SPDM_MESSAGE_VERSION_13)
        {
            spdm_request->other_params_support =
                context->local_context.algorithm.other_params_support;
            spdm_request->mel_specification =
                context->local_context.algorithm.mel_spec;
        }
    }

    if (spdm_request->header.spdm_version >= SPDM_MESSAGE_VERSION_13)
    {
        switch (context->connection_info.capability.flags &
                SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MULTI_KEY_CAP)
        {
            case 0:
                context->connection_info.multi_key_conn_rsp = false;
                break;
            case SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MULTI_KEY_CAP_ONLY:
                context->connection_info.multi_key_conn_rsp = true;
                break;
            case SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MULTI_KEY_CAP_NEG:
                context->connection_info.multi_key_conn_rsp =
                    (context->local_context.algorithm.other_params_support &
                     SPDM_ALGORITHMS_MULTI_KEY_CONN) != 0;
                break;
            default:
                co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
        }
        if (context->connection_info.multi_key_conn_rsp)
            spdm_request->other_params_support |= SPDM_ALGORITHMS_MULTI_KEY_CONN;
        else
            spdm_request->other_params_support &=
                static_cast<uint8_t>(~SPDM_ALGORITHMS_MULTI_KEY_CONN);
    }

    spdm_request->base_asym_algo =
        context->local_context.algorithm.base_asym_algo;
    spdm_request->base_hash_algo =
        context->local_context.algorithm.base_hash_algo;
    if (spdm_request->header.spdm_version >= SPDM_MESSAGE_VERSION_14)
        spdm_request->pqc_asym_algo =
            context->local_context.algorithm.pqc_asym_algo;
    spdm_request->ext_asym_count = 0;
    spdm_request->ext_hash_count = 0;

    if (spdm_request->header.spdm_version >= SPDM_MESSAGE_VERSION_11)
    {
        if (context->local_context.algorithm.dhe_named_group != 0)
        {
            spdm_request->struct_table[index].alg_type =
                SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_DHE;
            spdm_request->struct_table[index].alg_count = k_alg_count_fixed2_ext0;
            spdm_request->struct_table[index].alg_supported =
                context->local_context.algorithm.dhe_named_group;
            index++;
        }
        if (context->local_context.algorithm.aead_cipher_suite != 0)
        {
            spdm_request->struct_table[index].alg_type =
                SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_AEAD;
            spdm_request->struct_table[index].alg_count = k_alg_count_fixed2_ext0;
            spdm_request->struct_table[index].alg_supported =
                context->local_context.algorithm.aead_cipher_suite;
            index++;
        }
        if (context->local_context.algorithm.req_base_asym_alg != 0)
        {
            spdm_request->struct_table[index].alg_type =
                SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_REQ_BASE_ASYM_ALG;
            spdm_request->struct_table[index].alg_count = k_alg_count_fixed2_ext0;
            spdm_request->struct_table[index].alg_supported =
                context->local_context.algorithm.req_base_asym_alg;
            index++;
        }
        if (context->local_context.algorithm.key_schedule != 0)
        {
            spdm_request->struct_table[index].alg_type =
                SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_KEY_SCHEDULE;
            spdm_request->struct_table[index].alg_count = k_alg_count_fixed2_ext0;
            spdm_request->struct_table[index].alg_supported =
                context->local_context.algorithm.key_schedule;
            index++;
        }
        if (spdm_request->header.spdm_version >= SPDM_MESSAGE_VERSION_14)
        {
            if (context->local_context.algorithm.req_pqc_asym_alg != 0)
            {
                LIBSPDM_ASSERT(
                    context->local_context.algorithm.req_pqc_asym_alg <=
                    UINT16_MAX);
                spdm_request->struct_table[index].alg_type =
                    SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_REQ_PQC_ASYM_ALG;
                spdm_request->struct_table[index].alg_count = k_alg_count_fixed2_ext0;
                spdm_request->struct_table[index].alg_supported =
                    static_cast<uint16_t>(
                        context->local_context.algorithm.req_pqc_asym_alg);
                index++;
            }
            if (context->local_context.algorithm.kem_alg != 0)
            {
                LIBSPDM_ASSERT(
                    context->local_context.algorithm.kem_alg <= UINT16_MAX);
                spdm_request->struct_table[index].alg_type =
                    SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_KEM_ALG;
                spdm_request->struct_table[index].alg_count = k_alg_count_fixed2_ext0;
                spdm_request->struct_table[index].alg_supported =
                    static_cast<uint16_t>(
                        context->local_context.algorithm.kem_alg);
                index++;
            }
        }
        LIBSPDM_ASSERT(index == spdm_request->header.param1);
    }
    spdm_request_size = spdm_request->length;

    status = co_await libspdm_send_request_async(
        context, nullptr, false, spdm_request_size, spdm_request, io);
    // Release the sender buffer now (before acquiring the receiver buffer).
    // Dismiss the guard so the destructor does not double-release.
    release_sender.dismiss();
    libspdm_release_sender_buffer(context);
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;
    spdm_request =
        reinterpret_cast<libspdm_negotiate_algorithms_request_async_t*>(
            context->last_spdm_request);

    status = libspdm_acquire_receiver_buffer(
        context, &message_size, detail::as_void_pp(&message));
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    // RAII guard: release receiver buffer on every exit path from here.
    ScopeExit release_recv([&] { libspdm_release_receiver_buffer(context); });

    LIBSPDM_ASSERT(message_size >= transport_header_size);
    spdm_response =
        reinterpret_cast<libspdm_algorithms_response_async_t*>(message);
    spdm_response_size = message_size;

    status = co_await libspdm_receive_response_async(
        context, nullptr, false, &spdm_response_size,
        detail::as_void_pp(&spdm_response), io);
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    if (spdm_response_size < sizeof(spdm_message_header_t))
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    if (spdm_response->header.request_response_code == SPDM_ERROR)
    {
        status = libspdm_handle_simple_error_response(
            context, spdm_response->header.param1);
        if (LIBSPDM_STATUS_IS_ERROR(status))
            co_return status;
    }
    else if (spdm_response->header.request_response_code != SPDM_ALGORITHMS)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    if (spdm_response->header.spdm_version != spdm_request->header.spdm_version)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    if (spdm_response_size < sizeof(spdm_algorithms_response_t))
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    if (!libspdm_onehot0(spdm_response->measurement_specification_sel))
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    if (spdm_request->header.spdm_version >= SPDM_MESSAGE_VERSION_12)
    {
        if (!libspdm_onehot0(spdm_response->other_params_selection &
                             SPDM_ALGORITHMS_OPAQUE_DATA_FORMAT_MASK))
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
    }

    if (!libspdm_onehot0(spdm_response->measurement_hash_algo))
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    if (!libspdm_onehot0(spdm_response->base_asym_sel))
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    if (!libspdm_onehot0(spdm_response->base_hash_sel))
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    if (spdm_request->header.spdm_version >= SPDM_MESSAGE_VERSION_14)
    {
        if (!libspdm_onehot0(spdm_response->pqc_asym_sel))
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
    }

    if (spdm_response->ext_asym_sel_count > 0)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    if (spdm_response->ext_hash_sel_count > 0)
        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

    if (spdm_response_size <
        sizeof(spdm_algorithms_response_t) +
            sizeof(uint32_t) *
                static_cast<size_t>(spdm_response->ext_asym_sel_count) +
            sizeof(uint32_t) *
                static_cast<size_t>(spdm_response->ext_hash_sel_count) +
            sizeof(spdm_negotiate_algorithms_common_struct_table_t) *
                static_cast<size_t>(spdm_response->header.param1))
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    // Pointer to the first struct-table entry; reused by both validation and
    // dispatch passes — computed once via the shared helper.
    const auto* const st_begin = resp_struct_table_begin(spdm_response);
    const spdm_negotiate_algorithms_common_struct_table_t* struct_table =
        st_begin;

    if (spdm_request->header.spdm_version >= SPDM_MESSAGE_VERSION_11)
    {
        uint8_t alg_type_pre = struct_table->alg_type;
        for (index = 0; index < spdm_response->header.param1; index++)
        {
            if (reinterpret_cast<uintptr_t>(spdm_response) +
                    spdm_response_size <
                reinterpret_cast<uintptr_t>(struct_table))
                co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

            if (reinterpret_cast<uintptr_t>(spdm_response) +
                        spdm_response_size -
                        reinterpret_cast<uintptr_t>(struct_table) <
                    sizeof(spdm_negotiate_algorithms_common_struct_table_t))
                co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

            if ((struct_table->alg_type <
                 SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_DHE) ||
                (struct_table->alg_type >
                 SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_KEM_ALG))
                co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

            if ((spdm_response->header.spdm_version <
                 SPDM_MESSAGE_VERSION_14) &&
                (struct_table->alg_type >
                 SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_KEY_SCHEDULE))
                co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

            if ((index != 0) && (struct_table->alg_type <= alg_type_pre))
                co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

            alg_type_pre = struct_table->alg_type;
            const uint8_t fixed_alg_size = (struct_table->alg_count >> 4) & 0xFu;
            const uint8_t ext_alg_count  =  struct_table->alg_count        & 0xFu;

            if (fixed_alg_size != 2)
                co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

            if (ext_alg_count > 0)
                co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

            if (!libspdm_onehot0(struct_table->alg_supported))
                co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

            if (reinterpret_cast<uintptr_t>(spdm_response) +
                        spdm_response_size -
                        reinterpret_cast<uintptr_t>(struct_table) -
                        sizeof(spdm_negotiate_algorithms_common_struct_table_t) <
                    sizeof(uint32_t) * static_cast<size_t>(ext_alg_count))
                co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

            struct_table = next_struct_table(struct_table);
        }
    }

    spdm_response_size = reinterpret_cast<uintptr_t>(struct_table) -
                         reinterpret_cast<uintptr_t>(spdm_response);
    if (spdm_response_size != spdm_response->length)
        co_return LIBSPDM_STATUS_INVALID_MSG_SIZE;

    status = libspdm_append_message_a(context, spdm_request, spdm_request_size);
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    status = libspdm_append_message_a(context, spdm_response, spdm_response_size);
    if (LIBSPDM_STATUS_IS_ERROR(status))
        co_return status;

    context->connection_info.algorithm.measurement_spec =
        spdm_response->measurement_specification_sel;
    if (spdm_response->header.spdm_version >= SPDM_MESSAGE_VERSION_12)
    {
        context->connection_info.algorithm.other_params_support =
            spdm_response->other_params_selection &
            SPDM_ALGORITHMS_OPAQUE_DATA_FORMAT_MASK;
        if (spdm_response->header.spdm_version >= SPDM_MESSAGE_VERSION_13)
        {
            context->connection_info.algorithm.other_params_support =
                spdm_response->other_params_selection;
            context->connection_info.algorithm.mel_spec =
                spdm_response->mel_specification_sel;
        }
    }
    context->connection_info.algorithm.measurement_hash_algo =
        spdm_response->measurement_hash_algo;
    context->connection_info.algorithm.base_asym_algo = spdm_response->base_asym_sel;
    context->connection_info.algorithm.base_hash_algo = spdm_response->base_hash_sel;
    if (spdm_response->header.spdm_version >= SPDM_MESSAGE_VERSION_14)
        context->connection_info.algorithm.pqc_asym_algo =
            spdm_response->pqc_asym_sel;

    if (libspdm_is_capabilities_flag_supported(
            context, true, 0,
            SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEAS_CAP) &&
        (spdm_request->measurement_specification != 0))
    {
        if (context->connection_info.algorithm.measurement_spec !=
            SPDM_MEASUREMENT_SPECIFICATION_DMTF)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;

        const uint32_t meas_hash_size = libspdm_get_measurement_hash_size(
            context->connection_info.algorithm.measurement_hash_algo);
        if (meas_hash_size == 0)
            co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
    }
    else
    {
        if (context->connection_info.algorithm.measurement_spec != 0)
            co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
    }

    if (needs_base_hash(context))
    {
        const uint32_t hash_size = libspdm_get_hash_size(
            context->connection_info.algorithm.base_hash_algo);
        if (hash_size == 0)
            co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
        if ((context->connection_info.algorithm.base_hash_algo &
             context->local_context.algorithm.base_hash_algo) == 0)
            co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
    }

    if (needs_asym_algo(context))
    {
        const uint32_t asym_size = libspdm_get_asym_signature_size(
            context->connection_info.algorithm.base_asym_algo);
        const uint32_t pqc_asym_size =
            (spdm_response->header.spdm_version >= SPDM_MESSAGE_VERSION_14)
                ? libspdm_get_pqc_asym_signature_size(
                      context->connection_info.algorithm.pqc_asym_algo)
                : 0u;
        if (((asym_size == 0) && (pqc_asym_size == 0)) ||
            ((asym_size != 0) && (pqc_asym_size != 0)))
            co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
        if ((asym_size != 0) &&
            ((context->connection_info.algorithm.base_asym_algo &
              context->local_context.algorithm.base_asym_algo) == 0))
            co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
        if ((pqc_asym_size != 0) &&
            ((context->connection_info.algorithm.pqc_asym_algo &
              context->local_context.algorithm.pqc_asym_algo) == 0))
            co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
    }

    if (spdm_response->header.spdm_version >= SPDM_MESSAGE_VERSION_11)
    {
        // Dispatch pass: reuse the already-computed struct-table start pointer.
        struct_table = st_begin;
        for (index = 0; index < spdm_response->header.param1; index++)
        {
            switch (struct_table->alg_type)
            {
                case SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_DHE:
                    context->connection_info.algorithm.dhe_named_group =
                        struct_table->alg_supported;
                    break;
                case SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_AEAD:
                    context->connection_info.algorithm.aead_cipher_suite =
                        struct_table->alg_supported;
                    break;
                case SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_REQ_BASE_ASYM_ALG:
                    context->connection_info.algorithm.req_base_asym_alg =
                        struct_table->alg_supported;
                    break;
                case SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_KEY_SCHEDULE:
                    context->connection_info.algorithm.key_schedule =
                        struct_table->alg_supported;
                    break;
                case SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_REQ_PQC_ASYM_ALG:
                    context->connection_info.algorithm.req_pqc_asym_alg =
                        struct_table->alg_supported;
                    break;
                case SPDM_NEGOTIATE_ALGORITHMS_STRUCT_TABLE_ALG_TYPE_KEM_ALG:
                    context->connection_info.algorithm.kem_alg =
                        struct_table->alg_supported;
                    break;
            }
            struct_table = next_struct_table(struct_table);
        }

        if (needs_key_exchange(context))
        {
            const uint32_t dhe_size = libspdm_get_dhe_pub_key_size(
                context->connection_info.algorithm.dhe_named_group);
            const uint32_t kem_size =
                (spdm_response->header.spdm_version >= SPDM_MESSAGE_VERSION_14)
                    ? libspdm_get_kem_encap_key_size(
                          context->connection_info.algorithm.kem_alg)
                    : 0u;
            if (((dhe_size == 0) && (kem_size == 0)) ||
                ((dhe_size != 0) && (kem_size != 0)))
                co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
            if ((dhe_size != 0) &&
                ((context->connection_info.algorithm.dhe_named_group &
                  context->local_context.algorithm.dhe_named_group) == 0))
                co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
            if ((kem_size != 0) &&
                ((context->connection_info.algorithm.kem_alg &
                  context->local_context.algorithm.kem_alg) == 0))
                co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
        }

        if (needs_aead(context))
        {
            const uint32_t aead_size = libspdm_get_aead_key_size(
                context->connection_info.algorithm.aead_cipher_suite);
            if (aead_size == 0)
                co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
            if ((context->connection_info.algorithm.aead_cipher_suite &
                 context->local_context.algorithm.aead_cipher_suite) == 0)
                co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
        }

        if (needs_mut_auth(context))
        {
            const uint32_t req_asym_size = libspdm_get_req_asym_signature_size(
                context->connection_info.algorithm.req_base_asym_alg);
            const uint32_t req_pqc_size =
                (spdm_response->header.spdm_version >= SPDM_MESSAGE_VERSION_14)
                    ? libspdm_get_req_pqc_asym_signature_size(
                          context->connection_info.algorithm.req_pqc_asym_alg)
                    : 0u;
            if (((req_asym_size == 0) && (req_pqc_size == 0)) ||
                ((req_asym_size != 0) && (req_pqc_size != 0)))
                co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
            if ((req_asym_size != 0) &&
                ((context->connection_info.algorithm.req_base_asym_alg &
                  context->local_context.algorithm.req_base_asym_alg) == 0))
                co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
            if ((req_pqc_size != 0) &&
                ((context->connection_info.algorithm.req_pqc_asym_alg &
                  context->local_context.algorithm.req_pqc_asym_alg) == 0))
                co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
        }

        if (needs_key_schedule(context))
        {
            if (context->connection_info.algorithm.key_schedule !=
                SPDM_ALGORITHMS_KEY_SCHEDULE_SPDM)
                co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
            if ((context->connection_info.algorithm.key_schedule &
                 context->local_context.algorithm.key_schedule) == 0)
                co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;

            if (spdm_response->header.spdm_version >= SPDM_MESSAGE_VERSION_13)
            {
                if (libspdm_is_capabilities_flag_supported(
                        context, true, 0,
                        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEL_CAP) &&
                    (spdm_request->mel_specification != 0))
                {
                    if (context->connection_info.algorithm.mel_spec !=
                        SPDM_MEL_SPECIFICATION_DMTF)
                        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
                }
                else
                {
                    if (context->connection_info.algorithm.mel_spec != 0)
                        co_return LIBSPDM_STATUS_INVALID_MSG_FIELD;
                }
            }
        }

        if (spdm_request->header.spdm_version >= SPDM_MESSAGE_VERSION_13)
        {
            if ((context->connection_info.algorithm.other_params_support &
                 SPDM_ALGORITHMS_MULTI_KEY_CONN) == 0)
            {
                if ((context->local_context.capability.flags &
                     SPDM_GET_CAPABILITIES_REQUEST_FLAGS_MULTI_KEY_CAP) ==
                    SPDM_GET_CAPABILITIES_REQUEST_FLAGS_MULTI_KEY_CAP_ONLY)
                    co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
                context->connection_info.multi_key_conn_req = false;
            }
            else
            {
                if ((context->local_context.capability.flags &
                     SPDM_GET_CAPABILITIES_REQUEST_FLAGS_MULTI_KEY_CAP) == 0)
                    co_return LIBSPDM_STATUS_NEGOTIATION_FAIL;
                context->connection_info.multi_key_conn_req = true;
            }
        }
    }
    else
    {
        context->connection_info.algorithm.dhe_named_group = 0;
        context->connection_info.algorithm.aead_cipher_suite = 0;
        context->connection_info.algorithm.req_base_asym_alg = 0;
        context->connection_info.algorithm.key_schedule = 0;
        context->connection_info.algorithm.other_params_support = 0;
        context->connection_info.algorithm.req_pqc_asym_alg = 0;
        context->connection_info.algorithm.kem_alg = 0;
    }

    LIBSPDM_DEBUG((LIBSPDM_DEBUG_INFO, "base_hash - 0x%08x\n",
                   context->connection_info.algorithm.base_hash_algo));
    LIBSPDM_DEBUG((LIBSPDM_DEBUG_INFO, "base_asym - 0x%08x\n",
                   context->connection_info.algorithm.base_asym_algo));
    LIBSPDM_DEBUG((LIBSPDM_DEBUG_INFO, "dhe - 0x%04x\n",
                   context->connection_info.algorithm.dhe_named_group));
    LIBSPDM_DEBUG((LIBSPDM_DEBUG_INFO, "aead - 0x%04x\n",
                   context->connection_info.algorithm.aead_cipher_suite));
    LIBSPDM_DEBUG((LIBSPDM_DEBUG_INFO, "req_asym - 0x%04x\n",
                   context->connection_info.algorithm.req_base_asym_alg));
    LIBSPDM_DEBUG((LIBSPDM_DEBUG_INFO, "pqc_asym - 0x%08x\n",
                   context->connection_info.algorithm.pqc_asym_algo));
    LIBSPDM_DEBUG((LIBSPDM_DEBUG_INFO, "req_pqc_asym - 0x%04x\n",
                   context->connection_info.algorithm.req_pqc_asym_alg));
    LIBSPDM_DEBUG((LIBSPDM_DEBUG_INFO, "kem - 0x%04x\n",
                   context->connection_info.algorithm.kem_alg));

    context->connection_info.connection_state =
        LIBSPDM_CONNECTION_STATE_NEGOTIATED;

#if LIBSPDM_ENABLE_MSG_LOG
    libspdm_append_msg_log(context, spdm_response, spdm_response_size);
#endif

    co_return LIBSPDM_STATUS_SUCCESS;
}

} // namespace detail

template <AsyncSpdmIO IO>
boost::asio::awaitable<libspdm_return_t>
    libspdm_negotiate_algorithms_async(void* spdm_context, IO& io)
{
    auto* context = static_cast<libspdm_context_t*>(spdm_context);
    context->crypto_request = false;

    size_t retry = context->retry_times;
    uint64_t retry_delay_us = context->retry_delay_time;
    libspdm_return_t status;

    do
    {
        status = co_await detail::libspdm_try_negotiate_algorithms_async(
            context, io);
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
