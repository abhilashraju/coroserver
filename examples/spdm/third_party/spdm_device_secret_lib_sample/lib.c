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

// #include "hal/library/responder/asymsignlib.h"
// #include "hal/library/responder/csrlib.h"
// #include "hal/library/responder/measlib.h"
// #include "hal/library/responder/key_pair_info.h"
// #include "hal/library/responder/psklib.h"
// #include "hal/library/responder/setcertlib.h"
// #include "hal/library/requester/reqasymsignlib.h"
// #include "hal/library/requester/psklib.h"

#if LIBSPDM_ENABLE_CAPABILITY_MEAS_CAP
libspdm_return_t libspdm_measurement_collection(
    void* spdm_context, const uint32_t* session_id,
    spdm_version_number_t spdm_version, uint8_t measurement_specification,
    uint32_t measurement_hash_algo, uint8_t mesurements_index,
    uint8_t request_attribute, const uint8_t* nonce, uint8_t slot_id_param,
    size_t request_context_size, const void* request_context,
    uint8_t* content_changed, uint8_t* device_measurement_count,
    void* device_measurement, size_t* device_measurement_size)
{
    /* SPDM Measurement Block Structure:
     * - Index (1 byte): Measurement index
     * - MeasurementSpecification (1 byte): DMTF measurement spec
     * - MeasurementSize (2 bytes): Size of measurement field (little-endian)
     * - Measurement (variable): The actual measurement data
     *   - DMTFSpecMeasurementValueType (1 byte)
     *   - DMTFSpecMeasurementValueSize (2 bytes)
     *   - DMTFSpecMeasurementValue (variable)
     */

    const uint8_t total_measurement_count = 3;
    const uint8_t hash_size = 48; // SHA-384 hash size

    /* Each measurement block structure:
     * Header (4 bytes): Index + MeasSpec + Size
     * Measurement (51 bytes): Type(1) + Size(2) + Hash(48)
     * Total per block: 55 bytes
     */
    const size_t measurement_value_size =
        1 + 2 + hash_size;              // Type + Size + Hash
    const size_t block_header_size = 4; // Index + MeasSpec + MeasSize
    const size_t single_block_size = block_header_size + measurement_value_size;

    // Sample hash values for measurements
    static const uint8_t measurement_hashes[3][48] = {
        // Measurement 1: Firmware version hash
        {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
         0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
         0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
         0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
         0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30},

        // Measurement 2: Configuration hash
        {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A,
         0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,
         0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E,
         0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
         0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60},

        // Measurement 3: Hardware identity
        {0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A,
         0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74,
         0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E,
         0x7F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
         0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90}};

    // Initialize content_changed to 0 (no change)
    if (content_changed != NULL)
    {
        *content_changed = 0;
    }

    // Handle measurement index 0 - return total count only
    if (mesurements_index == 0)
    {
        if (device_measurement_count != NULL)
        {
            *device_measurement_count = total_measurement_count;
        }
        if (device_measurement_size != NULL)
        {
            *device_measurement_size = 0;
        }
        return LIBSPDM_STATUS_SUCCESS;
    }

    // Handle measurement index 0xFF - return all measurements
    if (mesurements_index == 0xFF)
    {
        size_t total_size = single_block_size * total_measurement_count;

        if (device_measurement_count != NULL)
        {
            *device_measurement_count = total_measurement_count;
        }

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
                // Block header
                output[offset++] = i + 1;      // Index (1-based)
                output[offset++] =
                    measurement_specification; // MeasurementSpecification
                output[offset++] =
                    (uint8_t)(measurement_value_size & 0xFF); // Size low byte
                output[offset++] = (uint8_t)((measurement_value_size >> 8) &
                                             0xFF);           // Size high byte

                // Measurement value
                output[offset++] = 0x02; // DMTFSpecMeasurementValueType: Digest
                output[offset++] = (uint8_t)(hash_size & 0xFF); // Size low byte
                output[offset++] =
                    (uint8_t)((hash_size >> 8) & 0xFF); // Size high byte

                // Copy hash
                libspdm_copy_mem(&output[offset], hash_size,
                                 measurement_hashes[i], hash_size);
                offset += hash_size;
            }
        }

        return LIBSPDM_STATUS_SUCCESS;
    }

    // Handle specific measurement index (1-based)
    if (mesurements_index >= 1 && mesurements_index <= total_measurement_count)
    {
        if (device_measurement_count != NULL)
        {
            *device_measurement_count = 1;
        }

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

            // Block header
            output[offset++] = mesurements_index; // Index
            output[offset++] =
                measurement_specification;        // MeasurementSpecification
            output[offset++] =
                (uint8_t)(measurement_value_size & 0xFF); // Size low byte
            output[offset++] = (uint8_t)((measurement_value_size >> 8) &
                                         0xFF);           // Size high byte

            // Measurement value
            output[offset++] = 0x02; // DMTFSpecMeasurementValueType: Digest
            output[offset++] = (uint8_t)(hash_size & 0xFF); // Size low byte
            output[offset++] =
                (uint8_t)((hash_size >> 8) & 0xFF);         // Size high byte

            // Copy hash
            libspdm_copy_mem(&output[offset], hash_size,
                             measurement_hashes[mesurements_index - 1],
                             hash_size);
        }

        return LIBSPDM_STATUS_SUCCESS;
    }

    // Invalid measurement index
    return LIBSPDM_STATUS_INVALID_PARAMETER;
}

bool libspdm_measurement_opaque_data(
    void* spdm_context, const uint32_t* session_id,
    spdm_version_number_t spdm_version, uint8_t measurement_specification,
    uint32_t measurement_hash_algo, uint8_t measurement_index,
    uint8_t request_attribute, size_t request_context_size,
    const void* request_context, void* opaque_data, size_t* opaque_data_size)
{
    /* This function provides opaque data for SPDM measurement responses.
     * Opaque data is vendor-specific and optional per SPDM specification.
     *
     * For a basic implementation, we can either:
     * 1. Return no opaque data (size = 0)
     * 2. Return vendor-specific metadata
     *
     * This implementation returns no opaque data by default.
     * Customize this function to add vendor-specific information if needed.
     */

    if (opaque_data_size == NULL)
    {
        return false;
    }

    /* Option 1: No opaque data (recommended for basic implementation) */
    *opaque_data_size = 0;
    return true;

    /* Option 2: Example with vendor-specific opaque data
     * Uncomment and modify as needed:
     *
     * // Define vendor-specific opaque data structure
     * typedef struct {
     *     uint8_t vendor_id[2];      // Vendor identifier
     *     uint8_t data_length[2];    // Length of vendor data
     *     uint8_t vendor_data[16];   // Vendor-specific data
     * } vendor_opaque_data_t;
     *
     * vendor_opaque_data_t vendor_data = {
     *     .vendor_id = {0x1A, 0xF7},  // Example vendor ID (DMTF)
     *     .data_length = {0x10, 0x00}, // 16 bytes of data
     *     .vendor_data = {
     *         0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
     *         0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
     *     }
     * };
     *
     * size_t required_size = sizeof(vendor_opaque_data_t);
     *
     * // Check if buffer is large enough
     * if (*opaque_data_size < required_size) {
     *     *opaque_data_size = required_size;
     *     return false;
     * }
     *
     * // Copy opaque data to output buffer
     * if (opaque_data != NULL) {
     *     libspdm_copy_mem(opaque_data, *opaque_data_size,
     *                     &vendor_data, required_size);
     * }
     *
     * *opaque_data_size = required_size;
     * return true;
     */
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
    size_t request_context_size, const void* request_context, void* opaque_data,
    size_t* opaque_data_size)
{
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_CHAL_CAP */

#if LIBSPDM_ENABLE_CAPABILITY_CHAL_CAP
bool libspdm_encap_challenge_opaque_data(
    void* spdm_context, spdm_version_number_t spdm_version, uint8_t slot_id,
    size_t request_context_size, const void* request_context, void* opaque_data,
    size_t* opaque_data_size)
{
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_CHAL_CAP */

#if LIBSPDM_ENABLE_CAPABILITY_MEL_CAP
/*Collect the measurement extension log.*/
bool libspdm_measurement_extension_log_collection(
    void* spdm_context, uint8_t mel_specification,
    uint8_t measurement_specification, uint32_t measurement_hash_algo,
    void** spdm_mel, size_t* spdm_mel_size)
{
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_MEL_CAP */

#if LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP
bool libspdm_requester_data_sign(
    void* spdm_context, spdm_version_number_t spdm_version, uint8_t key_pair_id,
    uint8_t op_code, uint16_t req_base_asym_alg, uint32_t req_pqc_asym_alg,
    uint32_t base_hash_algo, bool is_data_hash, const uint8_t* message,
    size_t message_size, uint8_t* signature, size_t* sig_size)
{
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP */

bool libspdm_responder_data_sign(
    void* spdm_context, spdm_version_number_t spdm_version, uint8_t key_pair_id,
    uint8_t op_code, uint32_t base_asym_algo, uint32_t pqc_asym_algo,
    uint32_t base_hash_algo, bool is_data_hash, const uint8_t* message,
    size_t message_size, uint8_t* signature, size_t* sig_size)
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

bool libspdm_write_certificate_to_nvm(
    void* spdm_context, uint8_t slot_id, const void* cert_chain,
    size_t cert_chain_size, uint32_t base_hash_algo, uint32_t base_asym_algo,
    uint32_t pqc_asym_algo, bool* need_reset, bool* is_busy)
{
    return false;
}

#endif /* LIBSPDM_ENABLE_CAPABILITY_SET_CERT_CAP */

#if LIBSPDM_ENABLE_CAPABILITY_CSR_CAP
bool libspdm_gen_csr(
    void* spdm_context, uint32_t base_hash_algo, uint32_t base_asym_algo,
    bool* need_reset, const void* request, size_t request_size,
    uint8_t* requester_info, size_t requester_info_length, uint8_t* opaque_data,
    uint16_t opaque_data_length, size_t* csr_len, uint8_t* csr_pointer,
    bool is_device_cert_model, bool* is_busy, bool* unexpected_request)
{
    return false;
}

#if LIBSPDM_ENABLE_CAPABILITY_CSR_CAP_EX
bool libspdm_gen_csr_ex(
    void* spdm_context, uint32_t base_hash_algo, uint32_t base_asym_algo,
    uint32_t pqc_asym_algo, bool* need_reset, const void* request,
    size_t request_size, uint8_t* requester_info, size_t requester_info_length,
    uint8_t* opaque_data, uint16_t opaque_data_length, size_t* csr_len,
    uint8_t* csr_pointer, uint8_t req_cert_model, uint8_t* req_csr_tracking_tag,
    uint8_t req_key_pair_id, bool overwrite, bool* is_busy,
    bool* unexpected_request)
{
    return false;
}
#endif /*LIBSPDM_ENABLE_CAPABILITY_CSR_CAP_EX*/
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

/**
 * read the key pair info of the key_pair_id.
 *
 * @param  spdm_context               A pointer to the SPDM context.
 * @param  key_pair_id                Indicate which key pair ID's information
 * to retrieve.
 *
 * @param  capabilities               Indicate the capabilities of the requested
 * key pairs.
 * @param  key_usage_capabilities     Indicate the key usages the responder
 * allows.
 * @param  current_key_usage          Indicate the currently configured key
 * usage for the requested key pairs ID.
 * @param  asym_algo_capabilities     Indicate the asymmetric algorithms the
 * Responder supports for this key pair ID.
 * @param  current_asym_algo          Indicate the currently configured
 * asymmetric algorithm for this key pair ID.
 * @param  assoc_cert_slot_mask       This field is a bit mask representing the
 * currently associated certificate slots.
 * @param  public_key_info_len        On input, indicate the size in bytes of
 * the destination buffer to store. On output, indicate the size in bytes of the
 * public_key_info. It can be NULL, if public_key_info is not required.
 * @param  public_key_info            A pointer to a destination buffer to store
 * the public_key_info. It can be NULL, if public_key_info is not required.
 *
 * @retval true  get key pair info successfully.
 * @retval false get key pair info failed.
 **/
bool libspdm_read_key_pair_info(
    void* spdm_context, uint8_t key_pair_id, uint16_t* capabilities,
    uint16_t* key_usage_capabilities, uint16_t* current_key_usage,
    uint32_t* asym_algo_capabilities, uint32_t* current_asym_algo,
    uint32_t* pqc_asym_algo_capabilities, uint32_t* current_pqc_asym_algo,
    uint8_t* assoc_cert_slot_mask, uint16_t* public_key_info_len,
    uint8_t* public_key_info)
{
    return false;
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
#endif /* #if LIBSPDM_ENABLE_CAPABILITY_SET_KEY_PAIR_INFO_CAP */

#if LIBSPDM_ENABLE_CAPABILITY_GET_KEY_PAIR_INFO_CAP
/**
 * Read the total number of key pairs supported by the device.
 *
 * @param  spdm_context  A pointer to the SPDM context.
 *
 * @return The total number of key pairs.
 **/
uint8_t libspdm_read_total_key_pairs(void* spdm_context)
{
    /* Return 0 to indicate no key pairs are supported in this basic
     * implementation. Modify this to return the actual number of key pairs if
     * needed. */
    return 0;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_GET_KEY_PAIR_INFO_CAP */

#if LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP
/**
 * Determine if mutual authentication should start during challenge.
 *
 * @param  spdm_context           A pointer to the SPDM context.
 * @param  spdm_version           SPDM version number.
 * @param  slot_id                Slot ID of the certificate chain.
 * @param  request_context_size   Size of the request context.
 * @param  request_context        Pointer to the request context.
 *
 * @retval true   Start mutual authentication.
 * @retval false  Do not start mutual authentication.
 **/
bool libspdm_challenge_start_mut_auth(
    void* spdm_context, spdm_version_number_t spdm_version, uint8_t slot_id,
    size_t request_context_size, const void* request_context)
{
    /* Return false to indicate mutual authentication is not started by default.
     * Modify this to return true if mutual authentication is required. */
    return false;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP */

#if (LIBSPDM_ENABLE_CAPABILITY_KEY_EX_CAP) &&                                  \
    (LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP)
/**
 * Determine if mutual authentication should start during key exchange.
 *
 * @param  spdm_context          A pointer to the SPDM context.
 * @param  session_id            Session ID.
 * @param  spdm_version          SPDM version number.
 * @param  slot_id               Slot ID of the certificate chain.
 * @param  req_slot_id           Output: Requested slot ID for mutual
 * authentication.
 * @param  session_policy        Session policy.
 * @param  opaque_data_length    Length of opaque data.
 * @param  opaque_data           Pointer to opaque data.
 * @param  mandatory_mut_auth    Output: Whether mutual authentication is
 * mandatory.
 *
 * @return Mutual authentication policy (0 = no mutual auth, 1 = start mutual
 * auth).
 **/
uint8_t libspdm_key_exchange_start_mut_auth(
    void* spdm_context, uint32_t session_id, spdm_version_number_t spdm_version,
    uint8_t slot_id, uint8_t* req_slot_id, uint8_t session_policy,
    size_t opaque_data_length, const void* opaque_data,
    bool* mandatory_mut_auth)
{
    /* Set default values for mutual authentication */
    if (req_slot_id != NULL)
    {
        *req_slot_id = 0;
    }
    if (mandatory_mut_auth != NULL)
    {
        *mandatory_mut_auth = false;
    }

    /* Return 0 to indicate no mutual authentication by default.
     * Return 1 to start mutual authentication if needed. */
    return 0;
}
#endif /* (LIBSPDM_ENABLE_CAPABILITY_KEY_EX_CAP) &&                            \
          (LIBSPDM_ENABLE_CAPABILITY_MUT_AUTH_CAP) */

#if LIBSPDM_ENABLE_CAPABILITY_EVENT_CAP
/**
 * Generate the event list for SPDM event notification.
 *
 * @param  spdm_context        A pointer to the SPDM context.
 * @param  spdm_version        SPDM version number.
 * @param  session_id          Session ID.
 * @param  event_count         Output: Number of events in the list.
 * @param  events_list_size    Input/Output: Size of the events list buffer.
 * @param  events_list         Output: Pointer to the events list buffer.
 *
 * @retval true   Event list generated successfully.
 * @retval false  Failed to generate event list.
 **/
bool libspdm_generate_event_list(
    void* spdm_context, spdm_version_number_t spdm_version, uint32_t session_id,
    uint32_t* event_count, size_t* events_list_size, void* events_list)
{
    /* Return no events by default.
     * Modify this to generate actual events if event capability is needed. */
    if (event_count != NULL)
    {
        *event_count = 0;
    }
    if (events_list_size != NULL)
    {
        *events_list_size = 0;
    }

    return true;
}
#endif /* LIBSPDM_ENABLE_CAPABILITY_EVENT_CAP */

#ifdef LIBSPDM_ENABLE_CAPABILITY_ENDPOINT_INFO_CAP
libspdm_return_t libspdm_generate_device_endpoint_info(
    void* spdm_context, uint8_t sub_code, uint8_t request_attributes,
    uint32_t* endpoint_info_size, void* endpoint_info)
{
    return LIBSPDM_STATUS_UNSUPPORTED_CAP;
}
#endif /* #if LIBSPDM_ENABLE_CAPABILITY_ENDPOINT_INFO_CAP */
