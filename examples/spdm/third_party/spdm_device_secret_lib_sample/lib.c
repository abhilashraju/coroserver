/**
 *  Copyright Notice:
 *  Copyright 2021-2025 DMTF. All rights reserved.
 *  License: BSD 3-Clause License. For full text see link:
 * https://github.com/DMTF/libspdm/blob/main/LICENSE.md
 **/

/** @file
 * SPDM common library.
 * It follows the SPDM Specification.
 **/

#include "internal/libspdm_common_lib.h"

#include <string.h>

#if LIBSPDM_ENABLE_CAPABILITY_MEAS_CAP
libspdm_return_t libspdm_measurement_collection(
    void* spdm_context, const uint32_t* session_id,
    spdm_version_number_t spdm_version, uint8_t measurement_specification,
    uint32_t measurement_hash_algo, uint8_t mesurements_index,
    uint8_t request_attribute, const uint8_t* requester_nonce,
    uint8_t slot_id_param, size_t request_context_size,
    const void* request_context, uint8_t* content_changed,
    uint8_t* device_measurement_count, void* device_measurement,
    size_t* device_measurement_size)
{
    const uint8_t total_measurement_count = 3;
    const uint8_t hash_size = 48; // SHA-384 hash size

    const size_t measurement_value_size = 1 + 2 + hash_size; // Type + Size + Hash
    const size_t block_header_size = 4; // Index + MeasSpec + MeasSize
    const size_t single_block_size = block_header_size + measurement_value_size;

    static const uint8_t measurement_hashes[3][48] = {
        {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
         0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
         0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
         0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
         0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30},
        {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A,
         0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,
         0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E,
         0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
         0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60},
        {0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A,
         0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74,
         0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E,
         0x7F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
         0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90}};

    if (content_changed != NULL)
        *content_changed = 0;

    if (mesurements_index == 0)
    {
        if (device_measurement_count != NULL)
            *device_measurement_count = total_measurement_count;
        if (device_measurement_size != NULL)
            *device_measurement_size = 0;
        return LIBSPDM_STATUS_SUCCESS;
    }

    if (mesurements_index == 0xFF)
    {
        size_t total_size = single_block_size * total_measurement_count;
        if (device_measurement_count != NULL)
            *device_measurement_count = total_measurement_count;
        if (device_measurement_size != NULL)
        {
            if (*device_measurement_size < total_size)
            {
                *device_measurement_size = total_size;
                return LIBSPDM_STATUS_BUFFER_TOO_SMALL;
            }
            *device_measurement_size = total_size;
        }
        if (device_measurement != NULL)
        {
            uint8_t* output = (uint8_t*)device_measurement;
            size_t offset = 0;
            for (uint8_t i = 0; i < total_measurement_count; i++)
            {
                output[offset++] = i + 1;
                output[offset++] = measurement_specification;
                output[offset++] = (uint8_t)(measurement_value_size & 0xFF);
                output[offset++] = (uint8_t)((measurement_value_size >> 8) & 0xFF);
                output[offset++] = 0x02;
                output[offset++] = (uint8_t)(hash_size & 0xFF);
                output[offset++] = (uint8_t)((hash_size >> 8) & 0xFF);
                libspdm_copy_mem(&output[offset], hash_size,
                                 measurement_hashes[i], hash_size);
                offset += hash_size;
            }
        }
        return LIBSPDM_STATUS_SUCCESS;
    }

    if (mesurements_index >= 1 && mesurements_index <= total_measurement_count)
    {
        if (device_measurement_count != NULL)
            *device_measurement_count = 1;
        if (device_measurement_size != NULL)
        {
            if (*device_measurement_size < single_block_size)
            {
                *device_measurement_size = single_block_size;
                return LIBSPDM_STATUS_BUFFER_TOO_SMALL;
            }
            *device_measurement_size = single_block_size;
        }
        if (device_measurement != NULL)
        {
            uint8_t* output = (uint8_t*)device_measurement;
            size_t offset = 0;
            output[offset++] = mesurements_index;
            output[offset++] = measurement_specification;
            output[offset++] = (uint8_t)(measurement_value_size & 0xFF);
            output[offset++] = (uint8_t)((measurement_value_size >> 8) & 0xFF);
            output[offset++] = 0x02;
            output[offset++] = (uint8_t)(hash_size & 0xFF);
            output[offset++] = (uint8_t)((hash_size >> 8) & 0xFF);
            libspdm_copy_mem(&output[offset], hash_size,
                             measurement_hashes[mesurements_index - 1], hash_size);
        }
        return LIBSPDM_STATUS_SUCCESS;
    }

    return LIBSPDM_STATUS_INVALID_PARAMETER;
}

bool libspdm_measurement_opaque_data(
    void* spdm_context, const uint32_t* session_id,
    spdm_version_number_t spdm_version, uint8_t measurement_specification,
    uint32_t measurement_hash_algo, uint8_t measurement_index,
    uint8_t request_attribute, size_t request_context_size,
    const void* request_context, void* opaque_data, size_t* opaque_data_size)
{
    if (opaque_data_size == NULL)
        return false;
    *opaque_data_size = 0;
    return true;
}

bool libspdm_generate_measurement_summary_hash(
    void* spdm_context, spdm_version_number_t spdm_version,
    uint32_t base_hash_algo, uint8_t measurement_specification,
    uint32_t measurement_hash_algo, uint8_t measurement_summary_hash_type,
    uint8_t* measurement_summary_hash, uint32_t measurement_summary_hash_size)
{
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_MEAS_CAP */

#if LIBSPDM_ENABLE_CAPABILITY_CHAL_CAP
bool libspdm_challenge_opaque_data(
    void* spdm_context, spdm_version_number_t spdm_version, uint8_t slot_id,
    size_t request_context_size, const void* request_context,
    void* opaque_data, size_t* opaque_data_size)
{
    return false;
}

#if LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP
bool libspdm_challenge_start_mut_auth(
    void* spdm_context, spdm_version_number_t spdm_version, uint8_t slot_id,
    size_t request_context_size, const void* request_context)
{
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP */
#endif /* LIBSPDM_ENABLE_CAPABILITY_CHAL_CAP */

#if (LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP) || (LIBSPDM_ENABLE_CAPABILITY_ENDPOINT_INFO_CAP)
#if LIBSPDM_ENABLE_CAPABILITY_CHAL_CAP
bool libspdm_encap_challenge_opaque_data(
    void* spdm_context, spdm_version_number_t spdm_version, uint8_t slot_id,
    size_t request_context_size, const void* request_context,
    void* opaque_data, size_t* opaque_data_size)
{
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_CHAL_CAP */
#endif /* LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP || ENDPOINT_INFO_CAP */

#if LIBSPDM_ENABLE_CAPABILITY_MEL_CAP
bool libspdm_measurement_extension_log_collection(
    void* spdm_context, uint8_t mel_specification,
    uint8_t measurement_specification, uint32_t measurement_hash_algo,
    void** spdm_mel, size_t* spdm_mel_size)
{
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_MEL_CAP */

#if (LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP) || (LIBSPDM_ENABLE_CAPABILITY_ENDPOINT_INFO_CAP)
bool libspdm_requester_data_sign(
    void* spdm_context, spdm_version_number_t spdm_version,
    uint8_t key_pair_id, uint8_t op_code, uint16_t req_base_asym_alg,
    uint32_t req_pqc_asym_alg, uint32_t base_hash_algo, bool is_data_hash,
    const uint8_t* message, size_t message_size, uint8_t* signature,
    size_t* sig_size)
{
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP || ENDPOINT_INFO_CAP */

bool libspdm_responder_data_sign(
    void* spdm_context, spdm_version_number_t spdm_version,
    uint8_t key_pair_id, uint8_t op_code, uint32_t base_asym_algo,
    uint32_t pqc_asym_algo, uint32_t base_hash_algo, bool is_data_hash,
    const uint8_t* message, size_t message_size, uint8_t* signature,
    size_t* sig_size)
{
    return false;
}

#if LIBSPDM_ENABLE_CAPABILITY_PSK_CAP
bool libspdm_psk_handshake_secret_hkdf_expand(
    spdm_version_number_t spdm_version, uint32_t base_hash_algo,
    const uint8_t* psk_hint, size_t psk_hint_size, const uint8_t* info,
    size_t info_size, uint8_t* out, size_t out_size)
{
    return false;
}

bool libspdm_psk_master_secret_hkdf_expand(
    spdm_version_number_t spdm_version, uint32_t base_hash_algo,
    const uint8_t* psk_hint, size_t psk_hint_size, const uint8_t* info,
    size_t info_size, uint8_t* out, size_t out_size)
{
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_PSK_CAP */

#if LIBSPDM_ENABLE_CAPABILITY_SET_CERT_CAP
bool libspdm_is_in_trusted_environment(void* spdm_context)
{
    return false;
}

uint32_t libspdm_get_cert_chain_slot_storage_size(void* spdm_context,
                                                   uint8_t slot_id)
{
    return 0;
}

bool libspdm_update_local_cert_chain(
    void* spdm_context, uint8_t slot_id, uint32_t base_hash_algo,
    uint32_t base_asym_algo, uint32_t pqc_asym_algo, size_t hash_size,
    const void* old_cert_chain, size_t old_cert_chain_size,
    const void* cert_chain, size_t* cert_chain_size, uint8_t cert_model,
    bool* need_reset, bool* is_busy)
{
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_SET_CERT_CAP */

#if LIBSPDM_ENABLE_CAPABILITY_CSR_CAP
bool libspdm_gen_csr(
    void* spdm_context, uint32_t base_hash_algo, uint32_t base_asym_algo,
    uint32_t pqc_asym_algo, bool* need_reset, const void* request,
    size_t request_size, uint8_t* requester_info,
    size_t requester_info_length, uint8_t* opaque_data,
    uint16_t opaque_data_length, size_t* csr_len, uint8_t* csr_pointer,
    uint8_t req_cert_model, uint8_t* req_csr_tracking_tag,
    uint8_t req_key_pair_id, bool overwrite, bool* is_busy,
    bool* unexpected_request)
{
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_CSR_CAP */

#if LIBSPDM_ENABLE_CAPABILITY_EVENT_CAP
bool libspdm_event_get_types(
    void* spdm_context, spdm_version_number_t spdm_version, uint32_t session_id,
    void* supported_event_groups_list,
    uint32_t* supported_event_groups_list_len, uint8_t* event_group_count)
{
    return false;
}

bool libspdm_event_subscribe(
    void* spdm_context, spdm_version_number_t spdm_version, uint32_t session_id,
    uint8_t subscribe_type, uint8_t subscribe_event_group_count,
    uint32_t subscribe_list_len, const void* subscribe_list)
{
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_EVENT_CAP */

#if LIBSPDM_ENABLE_CAPABILITY_GET_KEY_PAIR_INFO_CAP
bool libspdm_read_key_pair_info(
    void* spdm_context, uint8_t key_pair_id, uint8_t* total_key_pairs,
    uint16_t* capabilities, uint16_t* key_usage_capabilities,
    uint16_t* current_key_usage, uint32_t* asym_algo_capabilities,
    uint32_t* current_asym_algo, uint32_t* pqc_asym_algo_capabilities,
    uint32_t* current_pqc_asym_algo, uint8_t* assoc_cert_slot_mask,
    uint16_t* public_key_info_len, uint8_t* public_key_info)
{
    if (total_key_pairs != NULL)
        *total_key_pairs = 0;
    return false;
}

uint8_t libspdm_read_total_key_pairs(void* spdm_context)
{
    return 0;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_GET_KEY_PAIR_INFO_CAP */

#if LIBSPDM_ENABLE_CAPABILITY_SET_KEY_PAIR_INFO_CAP
bool libspdm_write_key_pair_info(
    void* spdm_context, uint8_t key_pair_id, uint8_t operation,
    uint16_t desired_key_usage, uint32_t desired_asym_algo,
    uint32_t desired_pqc_asym_algo, uint8_t desired_assoc_cert_slot_mask,
    bool* need_reset)
{
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_SET_KEY_PAIR_INFO_CAP */
