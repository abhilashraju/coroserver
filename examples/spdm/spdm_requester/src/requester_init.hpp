#pragma once
#include "spdm_init.hpp"
#include "spdm_utils.hpp"
#include "spdmglobal.hpp"
extern "C"
{
#include "library/spdm_common_lib.h"
#include "library/spdm_lib_config.h"
#include "library/spdm_requester_lib.h"
#include "library/spdm_return_status.h"
}

void updateFlags(uint32_t requester_capabilities_flag,
                 uint32_t responder_capabilities_flag)
{
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
}
bool spdmClientInit(void* spdm_context)
{
    uint8_t index;
    libspdm_return_t status;

    libspdm_data_parameter_t parameter;
    uint8_t data8;
    uint16_t data16;
    uint32_t data32;
    spdm_version_number_t spdm_version;
    uint32_t requester_capabilities_flag;
    uint32_t responder_capabilities_flag;

    if (m_use_version != 0)
    {
        spdm_version = m_use_version << SPDM_VERSION_NUMBER_SHIFT_BIT;
        setLocalData(spdm_context, LIBSPDM_DATA_SPDM_VERSION, spdm_version);
    }

    if (m_use_secured_message_version != 0)
    {
        spdm_version = m_use_secured_message_version
                       << SPDM_VERSION_NUMBER_SHIFT_BIT;
        setLocalData(spdm_context, LIBSPDM_DATA_SECURED_MESSAGE_VERSION,
                     spdm_version);
    }

    setLocalData(spdm_context, LIBSPDM_DATA_CAPABILITY_CT_EXPONENT, (uint8_t)0);
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
    setLocalData(spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS, data32);

    data8 = m_support_measurement_spec;
    setLocalData(spdm_context, LIBSPDM_DATA_MEASUREMENT_SPEC, data8);

    data32 = m_support_asym_algo;
    setLocalData(spdm_context, LIBSPDM_DATA_BASE_ASYM_ALGO, data32);

    data32 = m_support_hash_algo;
    setLocalData(spdm_context, LIBSPDM_DATA_BASE_HASH_ALGO, data32);

    data16 = m_support_dhe_algo;
    setLocalData(spdm_context, LIBSPDM_DATA_DHE_NAME_GROUP, data16);

    data16 = m_support_aead_algo;
    setLocalData(spdm_context, LIBSPDM_DATA_AEAD_CIPHER_SUITE, data16);

    data16 = m_support_req_asym_algo;
    setLocalData(spdm_context, LIBSPDM_DATA_REQ_BASE_ASYM_ALG, data16);

    data16 = m_support_key_schedule_algo;
    setLocalData(spdm_context, LIBSPDM_DATA_KEY_SCHEDULE, data16);

    data8 = m_support_other_params_support;
    setLocalData(spdm_context, LIBSPDM_DATA_OTHER_PARAMS_SUPPORT, data8);

    data8 = m_support_mel_spec;
    setLocalData(spdm_context, LIBSPDM_DATA_MEL_SPEC, data8);

    /* Skip if state is loaded*/
    status = libspdm_init_connection(spdm_context, false);
    if (LIBSPDM_STATUS_IS_ERROR(status))
    {
        LOG_ERROR("spdmClientInit - libspdm_init_connection failed{}",
                  (uint32_t)status);
        return false;
    }

    if (m_use_version == 0)
    {
        getRemoteData(spdm_context, LIBSPDM_DATA_SPDM_VERSION, spdm_version);
        m_use_version = spdm_version >> SPDM_VERSION_NUMBER_SHIFT_BIT;
    }

    /*get requester_capabilities_flag*/
    getLocalData(spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS,
                 requester_capabilities_flag);

    /*get responder_capabilities_flag*/
    getRemoteData(spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS,
                  responder_capabilities_flag);
    updateFlags(requester_capabilities_flag, responder_capabilities_flag);

    getRemoteData(spdm_context, LIBSPDM_DATA_CONNECTION_STATE, data32);
    LIBSPDM_ASSERT(data32 == LIBSPDM_CONNECTION_STATE_NEGOTIATED);

    getRemoteData(spdm_context, LIBSPDM_DATA_MEASUREMENT_HASH_ALGO,
                  m_use_measurement_hash_algo);
    getRemoteData(spdm_context, LIBSPDM_DATA_BASE_ASYM_ALGO, m_use_asym_algo);
    getRemoteData(spdm_context, LIBSPDM_DATA_BASE_HASH_ALGO, m_use_hash_algo);
    getRemoteData(spdm_context, LIBSPDM_DATA_REQ_BASE_ASYM_ALG,
                  m_use_req_asym_algo);

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
    LOG_INFO("slot_id {}", m_use_slot_id);
    LOG_INFO("req_slot_id {}", m_use_req_slot_id);

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
    setLocalData(spdm_context, LIBSPDM_DATA_LOCAL_SUPPORTED_SLOT_MASK, data8);
    return true;
}
