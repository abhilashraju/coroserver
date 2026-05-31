#pragma once
#include "cert_generator.hpp"
#include "spdm_init.hpp"
#include "spdm_utils.hpp"

#include <memory>

bool setCertificateChain(void* spdm_context, uint8_t slot_id)
{
    // Use unique_ptr with custom deleter for automatic memory management
    std::unique_ptr<spdm_certificate_info_t, decltype(&free)>
        requester_cert_chain_buffer(
            static_cast<spdm_certificate_info_t*>(
                malloc(SPDM_MAX_CERTIFICATE_CHAIN_SIZE)),
            free);
    if (requester_cert_chain_buffer == nullptr)
    {
        return false;
    }
    libspdm_register_cert_chain_buffer(spdm_context,
                                       requester_cert_chain_buffer.get(),
                                       SPDM_MAX_CERTIFICATE_CHAIN_SIZE);

    /* Load responder certificate chain from disk into the registered buffer
       and publish its digest so the requester can consume it. Uses
       std::ifstream to read the DER/PEM file in binary. Adjust cert_chain_path
       as needed. */
    {
        const char* cert_chain_path = "/etc/ssl/certs/https/server_cert.pem";
        std::vector<uint8_t> derData;
        if (reactor::pemCertFileToDerBuffer(cert_chain_path, derData))
        {
            size_t cert_chain_size = derData.size();
            if (cert_chain_size > 0 &&
                cert_chain_size <= SPDM_MAX_CERTIFICATE_CHAIN_SIZE)
            {
                memcpy(requester_cert_chain_buffer.get(), derData.data(),
                       cert_chain_size);

                libspdm_data_parameter_t parameter;
                /* install cert chain into SPDM context so responder serves it
                 */
                libspdm_zero_mem(&parameter, sizeof(parameter));
                parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
                parameter.additional_data[0] = slot_id;
                /* use LIBSPDM_DATA_LOCAL_PUBLIC_CERT_CHAIN */
                libspdm_set_data(spdm_context,
                                 LIBSPDM_DATA_LOCAL_PUBLIC_CERT_CHAIN,
                                 &parameter, requester_cert_chain_buffer.get(),
                                 cert_chain_size);
                /* The certificate chain is already set via
                   LIBSPDM_DATA_LOCAL_PUBLIC_CERT_CHAIN above. libspdm will
                   automatically compute and cache the hash internally. No need
                   to manually set the hash. */
                return true;
            }
        }
    }
    return false;
}
bool spdmResponderInit(void* spdm_context)
{
    libspdm_data_parameter_t parameter;
    uint8_t data8;
    // uint16_t data16;
    // uint32_t data32;
    spdm_version_number_t spdm_version;
    // libspdm_return_t status;

    size_t scratch_buffer_size =
        libspdm_get_sizeof_required_scratch_buffer(spdm_context);

    // Use unique_ptr with custom deleter for automatic memory management
    std::unique_ptr<void, decltype(&free)> m_scratch_buffer(
        malloc(scratch_buffer_size), free);
    if (m_scratch_buffer == nullptr)
    {
        return false;
    }
    libspdm_set_scratch_buffer(spdm_context, m_scratch_buffer.get(),
                               scratch_buffer_size);

    std::unique_ptr<void, decltype(&free)> requester_cert_chain_buffer(
        malloc(SPDM_MAX_CERTIFICATE_CHAIN_SIZE), free);
    if (requester_cert_chain_buffer == nullptr)
    {
        return false;
    }
    libspdm_register_cert_chain_buffer(spdm_context,
                                       requester_cert_chain_buffer.get(),
                                       SPDM_MAX_CERTIFICATE_CHAIN_SIZE);

    if (m_use_version != 0)
    {
        libspdm_zero_mem(&parameter, sizeof(parameter));
        parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
        spdm_version = m_use_version << SPDM_VERSION_NUMBER_SHIFT_BIT;
        libspdm_set_data(spdm_context, LIBSPDM_DATA_SPDM_VERSION, &parameter,
                         &spdm_version, sizeof(spdm_version));
    }

    if (m_use_secured_message_version != 0)
    {
        libspdm_zero_mem(&parameter, sizeof(parameter));
        parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
        spdm_version = m_use_secured_message_version
                       << SPDM_VERSION_NUMBER_SHIFT_BIT;
        libspdm_set_data(spdm_context, LIBSPDM_DATA_SECURED_MESSAGE_VERSION,
                         &parameter, &spdm_version, sizeof(spdm_version));
    }

    libspdm_zero_mem(&parameter, sizeof(parameter));
    parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;

    data8 = 0;
    setLocalData(spdm_context, LIBSPDM_DATA_CAPABILITY_CT_EXPONENT, data8);
    setLocalData(spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS,
                 m_use_responder_capability_flags);
    setLocalData(spdm_context, LIBSPDM_DATA_MEASUREMENT_SPEC,
                 m_support_measurement_spec);
    setLocalData(spdm_context, LIBSPDM_DATA_MEASUREMENT_HASH_ALGO,
                 m_support_measurement_hash_algo);
    setLocalData(spdm_context, LIBSPDM_DATA_BASE_ASYM_ALGO,
                 m_support_asym_algo);
    setLocalData(spdm_context, LIBSPDM_DATA_BASE_HASH_ALGO,
                 m_support_hash_algo);
    setLocalData(spdm_context, LIBSPDM_DATA_DHE_NAME_GROUP, m_support_dhe_algo);
    setLocalData(spdm_context, LIBSPDM_DATA_AEAD_CIPHER_SUITE,
                 m_support_aead_algo);
    setLocalData(spdm_context, LIBSPDM_DATA_REQ_BASE_ASYM_ALG,
                 m_support_req_asym_algo);
    setLocalData(spdm_context, LIBSPDM_DATA_KEY_SCHEDULE,
                 m_support_key_schedule_algo);
    setLocalData(spdm_context, LIBSPDM_DATA_OTHER_PARAMS_SUPPORT,
                 m_support_other_params_support);
    setLocalData(spdm_context, LIBSPDM_DATA_MEL_SPEC, m_support_mel_spec);

    data8 = 0xF0;
    setLocalData(spdm_context, LIBSPDM_DATA_HEARTBEAT_PERIOD, data8);
    return setCertificateChain(spdm_context, 0);
}
