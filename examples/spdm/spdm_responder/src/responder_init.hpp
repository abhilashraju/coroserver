#pragma once
#include "cert_generator.hpp"
#include "spdm_init.hpp"
#include "spdm_utils.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <algorithm>
#include <memory>

/* The SPDM certificate chain carries a hash of the root certificate whose
 * size depends on the negotiated base hash algorithm. The chain is installed
 * before negotiation, so the responder only offers the algorithm the chain
 * was built for. */
constexpr uint32_t responderChainHashAlgo =
    SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_384;
constexpr size_t responderChainHashSize = 48;

/**
 * Read all PEM certificates in a file and return them as DER blobs ordered
 * root first, as required by the SPDM certificate chain format.
 */
static bool readPemChainAsDer(const char* path,
                              std::vector<std::vector<uint8_t>>& ders)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in)
    {
        return false;
    }
    std::string pem((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    if (pem.empty())
    {
        return false;
    }
    reactor::BIOPtr bio =
        reactor::makeBIOPtr(BIO_new_mem_buf(pem.data(), (int)pem.size()));
    if (!bio)
    {
        return false;
    }
    std::vector<reactor::X509Ptr> certs;
    while (X509* raw = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr))
    {
        certs.push_back(reactor::makeX509Ptr(raw));
    }
    if (certs.empty())
    {
        return false;
    }
    // A TLS style bundle is leaf first; SPDM wants the root first.
    if (certs.size() > 1 &&
        X509_check_issued(certs.front().get(), certs.front().get()) !=
            X509_V_OK &&
        X509_check_issued(certs.back().get(), certs.back().get()) == X509_V_OK)
    {
        std::reverse(certs.begin(), certs.end());
    }
    for (auto& cert : certs)
    {
        int len = i2d_X509(cert.get(), nullptr);
        if (len <= 0)
        {
            return false;
        }
        std::vector<uint8_t> der(static_cast<size_t>(len));
        uint8_t* p = der.data();
        if (i2d_X509(cert.get(), &p) != len)
        {
            return false;
        }
        ders.push_back(std::move(der));
    }
    return true;
}

/**
 * Build an SPDM certificate chain (spdm_cert_chain_t header, hash of the root
 * certificate, then the DER certificates) into `chain` and install it as the
 * local public cert chain of `slot_id`.
 *
 * @param chain  Buffer of SPDM_MAX_CERTIFICATE_CHAIN_SIZE bytes owned by the
 *               caller. libspdm keeps a pointer to it, so it must outlive the
 *               SPDM context.
 */
bool setCertificateChain(void* spdm_context, uint8_t slot_id, void* chain)
{
    const char* cert_chain_path = "/etc/ssl/certs/https/server_cert.pem";
    std::vector<std::vector<uint8_t>> ders;
    if (chain == nullptr || !readPemChainAsDer(cert_chain_path, ders))
    {
        return false;
    }

    constexpr size_t headerSize = sizeof(spdm_cert_chain_t);
    size_t total = headerSize + responderChainHashSize;
    for (const auto& der : ders)
    {
        total += der.size();
    }
    if (total > SPDM_MAX_CERTIFICATE_CHAIN_SIZE)
    {
        return false;
    }

    auto* out = static_cast<uint8_t*>(chain);
    spdm_cert_chain_t header = {};
    header.length = static_cast<uint16_t>(total);
    memcpy(out, &header, headerSize);

    unsigned int hashLen = 0;
    if (EVP_Digest(ders.front().data(), ders.front().size(),
                   out + headerSize, &hashLen, EVP_sha384(),
                   nullptr) != 1 ||
        hashLen != responderChainHashSize)
    {
        return false;
    }

    size_t offset = headerSize + responderChainHashSize;
    for (const auto& der : ders)
    {
        memcpy(out + offset, der.data(), der.size());
        offset += der.size();
    }

    libspdm_data_parameter_t parameter;
    libspdm_zero_mem(&parameter, sizeof(parameter));
    parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
    parameter.additional_data[0] = slot_id;
    return libspdm_set_data(spdm_context, LIBSPDM_DATA_LOCAL_PUBLIC_CERT_CHAIN,
                            &parameter, chain, total) == LIBSPDM_STATUS_SUCCESS;
}
/**
 * @param spdm_context   Initialised libspdm responder context.
 * @param scratch_buffer Out: raw pointer to the allocated scratch buffer.
 *                       The CALLER must keep this allocation alive for the
 *                       entire lifetime of spdm_context.  Previously this was
 *                       a local unique_ptr which was freed on return, leaving
 *                       libspdm with a dangling pointer and causing heap
 *                       corruption (manifesting as INVALID_MSG_FIELD errors).
 * @param scratch_size   Out: size of the allocation stored in scratch_buffer.
 */
bool spdmResponderInit(void* spdm_context, void*& scratch_buffer,
                       size_t& scratch_size, void*& cert_chain_buffer)
{
    libspdm_data_parameter_t parameter;
    uint8_t data8;
    // uint16_t data16;
    // uint32_t data32;
    spdm_version_number_t spdm_version;
    // libspdm_return_t status;

    scratch_size = libspdm_get_sizeof_required_scratch_buffer(spdm_context);
    scratch_buffer = malloc(scratch_size);
    if (scratch_buffer == nullptr)
    {
        return false;
    }
    libspdm_set_scratch_buffer(spdm_context, scratch_buffer, scratch_size);

    // NOTE: cert_chain_buffer is the out-parameter — assigned here, owned by
    // the caller (SpdmProtocolHandler).  Same lifetime fix as scratch_buffer.
    cert_chain_buffer = malloc(SPDM_MAX_CERTIFICATE_CHAIN_SIZE);
    if (cert_chain_buffer == nullptr)
    {
        free(scratch_buffer);
        scratch_buffer = nullptr;
        return false;
    }
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
                 responderChainHashAlgo);
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
    return setCertificateChain(spdm_context, 0, cert_chain_buffer);
}
