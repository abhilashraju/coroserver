#pragma once
#include "spdm_init.hpp"
#include "spdmglobal.hpp"
extern "C"
{
#include "spdm_device_secret_lib_internal.h"

#include <libspdm/include/library/spdm_requester_lib.h>
}

bool spdmClientInit(void* spdm_context)
{
    uint8_t index;
    libspdm_return_t status;
    bool res;
    void* data;
    void* data1;
    size_t data_size;
    size_t data1_size;
    libspdm_data_parameter_t parameter;
    uint8_t data8;
    uint16_t data16;
    uint32_t data32;
    void* hash;
    void* hash1;
    size_t hash_size;
    size_t hash1_size;
    const uint8_t* root_cert;
    const uint8_t* root_cert1;
    size_t root_cert_size;
    size_t root_cert1_size;
    spdm_version_number_t spdm_version;
    size_t scratch_buffer_size;
    uint32_t requester_capabilities_flag;
    uint32_t responder_capabilities_flag;

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
    data32 = m_use_requester_capability_flags;
    if (m_use_req_slot_id == 0xFF)
    {
        data32 |= SPDM_GET_CAPABILITIES_REQUEST_FLAGS_PUB_KEY_ID_CAP;
        data32 &= ~SPDM_GET_CAPABILITIES_REQUEST_FLAGS_CERT_CAP;
        data32 &= ~SPDM_GET_CAPABILITIES_REQUEST_FLAGS_MULTI_KEY_CAP;
    }
    if (m_use_capability_flags != 0)
    {
        data32 = m_use_capability_flags;
        m_use_requester_capability_flags = m_use_capability_flags;
    }
    libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS, &parameter,
                     &data32, sizeof(data32));

    data8 = m_support_measurement_spec;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_MEASUREMENT_SPEC, &parameter,
                     &data8, sizeof(data8));
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

    /* Skip if state is loaded*/
    status = libspdm_init_connection(spdm_context, false);
    if (LIBSPDM_STATUS_IS_ERROR(status))
    {
        printf("libspdm_init_connection - 0x%x\n", (uint32_t)status);
        return false;
    }
    // if ((m_exe_connection & EXE_CONNECTION_VERSION_ONLY) != 0)
    // {
    //     /* GET_VERSION is done, handle special PSK use case*/
    //     status = spdm_provision_psk_version_only(spdm_context, true);
    //     if (LIBSPDM_STATUS_IS_ERROR(status))
    //     {
    //         return false;
    //     }
    // }

    if (m_use_version == 0)
    {
        libspdm_zero_mem(&parameter, sizeof(parameter));
        parameter.location = LIBSPDM_DATA_LOCATION_CONNECTION;
        data_size = sizeof(spdm_version);
        libspdm_get_data(spdm_context, LIBSPDM_DATA_SPDM_VERSION, &parameter,
                         &spdm_version, &data_size);
        m_use_version = spdm_version >> SPDM_VERSION_NUMBER_SHIFT_BIT;
    }

    /*get requester_capabilities_flag*/
    libspdm_zero_mem(&parameter, sizeof(parameter));
    parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
    data_size = sizeof(data32);
    libspdm_get_data(spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS, &parameter,
                     &data32, &data_size);
    requester_capabilities_flag = data32;

    /*get responder_capabilities_flag*/
    libspdm_zero_mem(&parameter, sizeof(parameter));
    parameter.location = LIBSPDM_DATA_LOCATION_CONNECTION;
    data_size = sizeof(data32);
    libspdm_get_data(spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS, &parameter,
                     &data32, &data_size);
    responder_capabilities_flag = data32;

    /*change m_exe_connection and m_exe_session base on responder/requester
     * supported capabilities*/
    if ((SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CERT_CAP &
         responder_capabilities_flag) == 0)
    {
        m_exe_connection &= ~EXE_CONNECTION_DIGEST;
        m_exe_connection &= ~EXE_CONNECTION_CERT;
        m_exe_session &= ~EXE_SESSION_DIGEST;
        m_exe_session &= ~EXE_SESSION_CERT;
    }
    if ((SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CHAL_CAP &
         responder_capabilities_flag) == 0)
    {
        m_exe_connection &= ~EXE_CONNECTION_CHAL;
    }
    if ((SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEAS_CAP &
         responder_capabilities_flag) == 0)
    {
        m_exe_connection &= ~EXE_CONNECTION_MEAS;
        m_exe_session &= ~EXE_SESSION_MEAS;
    }
    if ((SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEL_CAP &
         responder_capabilities_flag) == 0)
    {
        m_exe_connection &= ~EXE_CONNECTION_MEL;
        m_exe_session &= ~EXE_SESSION_MEL;
    }

    if (((SPDM_GET_CAPABILITIES_REQUEST_FLAGS_KEY_EX_CAP &
          requester_capabilities_flag) == 0) ||
        ((SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_KEY_EX_CAP &
          responder_capabilities_flag) == 0))
    {
        m_exe_session &= ~EXE_SESSION_KEY_EX;
    }
    if (((SPDM_GET_CAPABILITIES_REQUEST_FLAGS_PSK_CAP &
          requester_capabilities_flag) == 0) ||
        ((SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PSK_CAP &
          responder_capabilities_flag) == 0))
    {
        m_exe_session &= ~EXE_SESSION_PSK;
    }
    if (((SPDM_GET_CAPABILITIES_REQUEST_FLAGS_KEY_UPD_CAP &
          requester_capabilities_flag) == 0) ||
        ((SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_KEY_UPD_CAP &
          responder_capabilities_flag) == 0))
    {
        m_exe_session &= ~EXE_SESSION_KEY_UPDATE;
    }
    if (((SPDM_GET_CAPABILITIES_REQUEST_FLAGS_HBEAT_CAP &
          requester_capabilities_flag) == 0) ||
        ((SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_HBEAT_CAP &
          responder_capabilities_flag) == 0))
    {
        m_exe_session &= ~EXE_SESSION_HEARTBEAT;
    }
    if ((SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_SET_CERT_CAP &
         responder_capabilities_flag) == 0)
    {
        m_exe_connection &= ~EXE_CONNECTION_SET_CERT;
        m_exe_session &= ~EXE_SESSION_SET_CERT;
    }
    if ((SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CSR_CAP &
         responder_capabilities_flag) == 0)
    {
        m_exe_connection &= ~EXE_CONNECTION_GET_CSR;
        m_exe_session &= ~EXE_SESSION_GET_CSR;
    }
    if ((SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_GET_KEY_PAIR_INFO_CAP &
         responder_capabilities_flag) == 0)
    {
        m_exe_connection &= ~EXE_CONNECTION_GET_KEY_PAIR_INFO;
        m_exe_session &= ~EXE_SESSION_GET_KEY_PAIR_INFO;
    }
    if ((SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_SET_KEY_PAIR_INFO_CAP &
         responder_capabilities_flag) == 0)
    {
        m_exe_connection &= ~EXE_CONNECTION_SET_KEY_PAIR_INFO;
        m_exe_session &= ~EXE_CONNECTION_SET_KEY_PAIR_INFO;
    }

    if ((SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_EP_INFO_CAP_SIG &
         responder_capabilities_flag) == 0)
    {
        m_exe_connection &= ~EXE_CONNECTION_EP_INFO;
        m_exe_session &= ~EXE_SESSION_EP_INFO;
    }

    data_size = sizeof(data32);
    libspdm_get_data(spdm_context, LIBSPDM_DATA_CONNECTION_STATE, &parameter,
                     &data32, &data_size);
    LIBSPDM_ASSERT(data32 == LIBSPDM_CONNECTION_STATE_NEGOTIATED);

    data_size = sizeof(data32);
    libspdm_get_data(spdm_context, LIBSPDM_DATA_MEASUREMENT_HASH_ALGO,
                     &parameter, &data32, &data_size);
    m_use_measurement_hash_algo = data32;
    data_size = sizeof(data32);
    libspdm_get_data(spdm_context, LIBSPDM_DATA_BASE_ASYM_ALGO, &parameter,
                     &data32, &data_size);
    m_use_asym_algo = data32;
    data_size = sizeof(data32);
    libspdm_get_data(spdm_context, LIBSPDM_DATA_BASE_HASH_ALGO, &parameter,
                     &data32, &data_size);
    m_use_hash_algo = data32;
    data_size = sizeof(data16);
    libspdm_get_data(spdm_context, LIBSPDM_DATA_REQ_BASE_ASYM_ALG, &parameter,
                     &data16, &data_size);
    m_use_req_asym_algo = data16;

    if ((m_use_requester_capability_flags &
         SPDM_GET_CAPABILITIES_REQUEST_FLAGS_PUB_KEY_ID_CAP) != 0)
    {
        m_use_req_slot_id = 0xFF;
    }
    if (((m_exe_connection & EXE_CONNECTION_CERT) == 0) &&
        (m_use_slot_id != 0xFF))
    {
        m_exe_connection &= ~EXE_CONNECTION_CHAL;
        m_exe_connection &= ~EXE_CONNECTION_MEAS;
        m_exe_connection &= ~EXE_CONNECTION_EP_INFO;
        m_exe_session &= ~EXE_SESSION_KEY_EX;
        m_exe_session &= ~EXE_SESSION_MEAS;
        m_exe_session &= ~EXE_SESSION_EP_INFO;
    }

    printf("slot_id - %x\n", m_use_slot_id);
    printf("req_slot_id - %x\n", m_use_req_slot_id);

    // if (m_use_slot_id == 0xFF)
    // {
    //     res = libspdm_read_responder_public_key(m_use_asym_algo, &data,
    //                                             &data_size);
    //     if (res)
    //     {
    //         libspdm_zero_mem(&parameter, sizeof(parameter));
    //         parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
    //         libspdm_set_data(spdm_context, LIBSPDM_DATA_PEER_PUBLIC_KEY,
    //                          &parameter, data, data_size);
    //         /* Do not free it.*/
    //     }
    //     else
    //     {
    //         printf("read_responder_public_key fail!\n");
    //         return false;
    //     }
    // }
    // else
    // {
    //     res = libspdm_read_responder_root_public_certificate(
    //         m_use_hash_algo, m_use_asym_algo, &data, &data_size, &hash,
    //         &hash_size);
    //     if (res)
    //     {
    //         libspdm_x509_get_cert_from_cert_chain(
    //             (uint8_t*)data + sizeof(spdm_cert_chain_t) + hash_size,
    //             data_size - sizeof(spdm_cert_chain_t) - hash_size, 0,
    //             &root_cert, &root_cert_size);
    //         libspdm_zero_mem(&parameter, sizeof(parameter));
    //         parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
    //         libspdm_set_data(spdm_context,
    //         LIBSPDM_DATA_PEER_PUBLIC_ROOT_CERT,
    //                          &parameter, (void*)root_cert, root_cert_size);
    //         /* Do not free it.*/
    //     }
    //     else
    //     {
    //         printf("read_responder_root_public_certificate fail!\n");
    //         return false;
    //     }
    //     res = libspdm_read_responder_root_public_certificate_slot(
    //         1, m_use_hash_algo, m_use_asym_algo, &data1, &data1_size, &hash1,
    //         &hash1_size);
    //     if (res)
    //     {
    //         libspdm_x509_get_cert_from_cert_chain(
    //             (uint8_t*)data1 + sizeof(spdm_cert_chain_t) + hash1_size,
    //             data1_size - sizeof(spdm_cert_chain_t) - hash1_size, 0,
    //             &root_cert1, &root_cert1_size);
    //         libspdm_zero_mem(&parameter, sizeof(parameter));
    //         parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
    //         libspdm_set_data(spdm_context,
    //         LIBSPDM_DATA_PEER_PUBLIC_ROOT_CERT,
    //                          &parameter, (void*)root_cert1, root_cert1_size);
    //         /* Do not free it.*/
    //     }
    //     else
    //     {
    //         printf("read_responder_root_public_certificate fail!\n");
    //         return false;
    //     }
    // }

    // if (m_use_req_slot_id == 0xFF)
    // {
    //     if (m_use_req_asym_algo != 0)
    //     {
    //         res = libspdm_read_requester_public_key(m_use_req_asym_algo,
    //         &data,
    //                                                 &data_size);
    //         if (res)
    //         {
    //             libspdm_zero_mem(&parameter, sizeof(parameter));
    //             parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
    //             libspdm_set_data(spdm_context, LIBSPDM_DATA_LOCAL_PUBLIC_KEY,
    //                              &parameter, data, data_size);
    //             /* Do not free it.*/
    //         }
    //         else
    //         {
    //             printf("read_requester_public_key fail!\n");
    //             return false;
    //         }
    //     }
    // }
    // else
    // {
    //     if (m_use_req_asym_algo != 0)
    //     {
    //         res = libspdm_read_requester_public_certificate_chain(
    //             m_use_hash_algo, m_use_req_asym_algo, &data, &data_size,
    //             NULL, NULL);
    //         if (res)
    //         {
    //             libspdm_zero_mem(&parameter, sizeof(parameter));
    //             parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;

    //             for (index = 0; index < m_use_slot_count; index++)
    //             {
    //                 parameter.additional_data[0] = index;
    //                 libspdm_set_data(spdm_context,
    //                                  LIBSPDM_DATA_LOCAL_PUBLIC_CERT_CHAIN,
    //                                  &parameter, data, data_size);
    //                 data8 = (uint8_t)(0xB0 + index);
    //                 libspdm_set_data(spdm_context,
    //                                  LIBSPDM_DATA_LOCAL_KEY_PAIR_ID,
    //                                  &parameter, &data8, sizeof(data8));
    //                 data8 = SPDM_CERTIFICATE_INFO_CERT_MODEL_DEVICE_CERT;
    //                 libspdm_set_data(spdm_context,
    //                 LIBSPDM_DATA_LOCAL_CERT_INFO,
    //                                  &parameter, &data8, sizeof(data8));
    //                 data16 = SPDM_KEY_USAGE_BIT_MASK_KEY_EX_USE |
    //                          SPDM_KEY_USAGE_BIT_MASK_CHALLENGE_USE |
    //                          SPDM_KEY_USAGE_BIT_MASK_MEASUREMENT_USE |
    //                          SPDM_KEY_USAGE_BIT_MASK_ENDPOINT_INFO_USE;
    //                 libspdm_set_data(spdm_context,
    //                                  LIBSPDM_DATA_LOCAL_KEY_USAGE_BIT_MASK,
    //                                  &parameter, &data16, sizeof(data16));
    //             }
    //             /* do not free it*/
    //         }
    //         else
    //         {
    //             printf("read_requester_public_certificate_chain fail!\n");
    //             return false;
    //         }
    //     }
    // }

    libspdm_zero_mem(&parameter, sizeof(parameter));
    parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
    data8 = 0;
    for (index = 0; index < m_use_slot_count; index++)
    {
        data8 |= (1 << index);
    }
    libspdm_set_data(spdm_context, LIBSPDM_DATA_LOCAL_SUPPORTED_SLOT_MASK,
                     &parameter, &data8, sizeof(data8));

    return true;
}
