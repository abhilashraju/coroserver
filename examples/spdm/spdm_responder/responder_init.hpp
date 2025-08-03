#pragma once
#include "spdm_init.hpp"
bool spdmResponderInit(void* spdm_context)
{
    libspdm_data_parameter_t parameter;
    uint8_t data8;
    uint16_t data16;
    uint32_t data32;
    spdm_version_number_t spdm_version;
    // libspdm_return_t status;

    size_t scratch_buffer_size =
        libspdm_get_sizeof_required_scratch_buffer(spdm_context);
    void* m_scratch_buffer = (void*)malloc(scratch_buffer_size);
    if (m_scratch_buffer == NULL)
    {
        return false;
    }
    libspdm_set_scratch_buffer(spdm_context, m_scratch_buffer,
                               scratch_buffer_size);
    void* requester_cert_chain_buffer;

    requester_cert_chain_buffer =
        (void*)malloc(SPDM_MAX_CERTIFICATE_CHAIN_SIZE);
    if (requester_cert_chain_buffer == NULL)
    {
        return false;
    }
    libspdm_register_cert_chain_buffer(spdm_context,
                                       requester_cert_chain_buffer,
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
    libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_CT_EXPONENT,
                     &parameter, &data8, sizeof(data8));
    data32 = m_use_responder_capability_flags;

    libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS, &parameter,
                     &data32, sizeof(data32));

    data8 = m_support_measurement_spec;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_MEASUREMENT_SPEC, &parameter,
                     &data8, sizeof(data8));
    data32 = m_support_measurement_hash_algo;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_MEASUREMENT_HASH_ALGO,
                     &parameter, &data32, sizeof(data32));
    data32 = m_support_asym_algo;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_BASE_ASYM_ALGO, &parameter,
                     &data32, sizeof(data32));
    data32 = m_support_hash_algo;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_BASE_HASH_ALGO, &parameter,
                     &data32, sizeof(data32));
    data16 = m_support_dhe_algo;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_DHE_NAME_GROUP, &parameter,
                     &data16, sizeof(data16));
    data16 = m_support_aead_algo;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_AEAD_CIPHER_SUITE, &parameter,
                     &data16, sizeof(data16));
    data16 = m_support_req_asym_algo;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_REQ_BASE_ASYM_ALG, &parameter,
                     &data16, sizeof(data16));
    data16 = m_support_key_schedule_algo;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_KEY_SCHEDULE, &parameter,
                     &data16, sizeof(data16));
    data8 = m_support_other_params_support;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_OTHER_PARAMS_SUPPORT,
                     &parameter, &data8, sizeof(data8));
    data8 = m_support_mel_spec;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_MEL_SPEC, &parameter, &data8,
                     sizeof(data8));

    data8 = 0xF0;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_HEARTBEAT_PERIOD, &parameter,
                     &data8, sizeof(data8));

    return true;
}
