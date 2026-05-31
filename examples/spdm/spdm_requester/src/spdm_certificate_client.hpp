#pragma once
#include "cert_exchange_requester.hpp"
#include "logger.hpp"
#include "spdmglobal.hpp"

extern "C"
{
#include <internal/libspdm_common_lib.h>
#include <library/spdm_requester_lib.h>
}

#ifndef LIBSPDM_MAX_CERT_CHAIN_SIZE
#define LIBSPDM_MAX_CERT_CHAIN_SIZE 0x1000
#endif

#include <memory>
#include <string>
#include <tuple>
#include <vector>

// Global timeout for device send/receive operations (10 seconds in
// microseconds)
constexpr uint64_t DEVICE_IO_TIMEOUT_CERT = 10000000;

// Handles SPDM certificate operations
class SpdmCertificateClient
{
  public:
    SpdmCertificateClient(void* spdmContext,
                          const std::string& trustStorePath = "/etc/ssl/certs")
    {
        spdmContext_ = spdmContext;
        trustStore_ =
            std::make_shared<FileCertificateTrustStore>(trustStorePath);

        // Initialize certificate exchange requester
        certExchangeRequester_ = std::make_unique<CertificateExchangeRequester>(
            spdmContext_, trustStore_, "requester");
    }

    // Set send/receive callback functions for certificate exchange
    void setSendFunction(
        std::function<libspdm_return_t(void*, size_t, const void*, uint64_t)>
            sendFunc)
    {
        certExchangeRequester_->setSendFunction(
            [sendFunc, this](const std::vector<uint8_t>& message) {
                libspdm_return_t status =
                    sendFunc(spdmContext_, message.size(), message.data(),
                             DEVICE_IO_TIMEOUT_CERT);
                return status == LIBSPDM_STATUS_SUCCESS;
            });
    }

    void setReceiveFunction(
        std::function<libspdm_return_t(void*, size_t*, void**, uint64_t)>
            receiveFunc)
    {
        certExchangeRequester_->setReceiveFunction(
            [receiveFunc, this]() -> std::pair<bool, std::vector<uint8_t>> {
                std::array<uint8_t, LIBSPDM_MAX_SPDM_MSG_SIZE> buffer{};
                size_t messageSize = buffer.size();
                void* messagePtr = buffer.data();

                libspdm_return_t status =
                    receiveFunc(spdmContext_, &messageSize, &messagePtr,
                                DEVICE_IO_TIMEOUT_CERT);
                if (status != LIBSPDM_STATUS_SUCCESS)
                {
                    return {false, std::vector<uint8_t>{}};
                }

                std::vector<uint8_t> message(buffer.begin(),
                                             buffer.begin() + messageSize);
                return {true, message};
            });
    }

    // Perform certificate exchange with responder
    std::tuple<bool, std::string> exchangeCertificates(
        const std::string& requesterCertPath =
            "/var/lib/spdm/certs/requester_cert.der")
    {
        LOG_INFO("Starting certificate exchange...");

        // Load requester certificate
        if (!certExchangeRequester_->loadRequesterCertificate(
                requesterCertPath, CertificateFormat::PEM))
        {
            LOG_ERROR("Failed to load requester certificate from {}",
                      requesterCertPath);
            return std::make_tuple(false, std::string());
        }

        // Perform the exchange
        bool success = certExchangeRequester_->performCertificateExchange();
        if (success)
        {
            // Get the stored certificate location from the trust store
            std::string certLocation =
                certExchangeRequester_->getStoredCertificatePath();
            return std::make_tuple(true, certLocation);
        }
        return std::make_tuple(false, std::string());
    }

    // Get certificate exchange requester for advanced operations
    CertificateExchangeRequester* getCertExchangeRequester()
    {
        return certExchangeRequester_.get();
    }

    using CertificateTuple =
        std::tuple<std::string, std::vector<uint8_t>, std::vector<uint8_t>>;
    using CertificateResult = std::pair<bool, CertificateTuple>;

    CertificateResult getCertificate(size_t slotId)
    {
        std::vector<uint8_t> certChain(LIBSPDM_MAX_CERT_CHAIN_SIZE);
        size_t certChainSize = certChain.size();

        libspdm_return_t status = libspdm_get_certificate(
            spdmContext_, nullptr, slotId, &certChainSize, certChain.data());

        if (LIBSPDM_STATUS_IS_ERROR(status))
        {
            LOG_ERROR("libspdm_get_certificate failed, status: 0x{:X}", status);
            return std::make_pair(false,
                                  CertificateTuple{"", std::vector<uint8_t>{},
                                                   std::vector<uint8_t>{}});
        }
        certChain.resize(certChainSize);
        size_t hash_size = 0;

        hash_size = libspdm_get_hash_size(
            reinterpret_cast<libspdm_context_t*>(spdmContext_)
                ->connection_info.algorithm.base_hash_algo);

        constexpr size_t spdm_cert_chain_header_size =
            4; // 2 bytes Length + 2 bytes Reserved
        if (certChain.size() < spdm_cert_chain_header_size + hash_size)
        {
            LOG_ERROR(
                "Certificate chain too small for header+hash: size={}, header+hash={}",
                certChain.size(), spdm_cert_chain_header_size + hash_size);
            return std::make_pair(false,
                                  CertificateTuple{"", std::vector<uint8_t>{},
                                                   std::vector<uint8_t>{}});
        }
        std::vector<uint8_t> derCerts(
            certChain.begin() + spdm_cert_chain_header_size + hash_size,
            certChain.end());
        const auto [pemChain, leafCert] = derCertsToPem(derCerts);
        return std::make_pair(true,
                              CertificateTuple{pemChain, certChain, leafCert});
    }

    using CertificateDigestType =
        std::tuple<uint8_t, std::vector<uint8_t>, size_t>;

    std::pair<bool, CertificateDigestType> getCertificateDigests()
    {
        LOG_INFO(
            "SPDM Requester: Calling libspdm_get_digest to retrieve certificate digests");

        // Buffer for digest response - each digest is 48 bytes based on SPDM
        // trace analysis
        constexpr size_t DIGEST_SIZE = 48; // Fixed size from SPDM trace
        constexpr size_t MAX_SLOTS = 8;
        std::vector<uint8_t> digestBuffer(MAX_SLOTS * DIGEST_SIZE);
        uint8_t slotMask = 0;

        auto status = libspdm_get_digest(
            spdmContext_, nullptr, // No session
            &slotMask,             // Output: which slots have certificates
            digestBuffer.data());  // Output: digest data buffer

        if (LIBSPDM_STATUS_IS_ERROR(status))
        {
            LOG_ERROR(
                "SPDM Requester: libspdm_get_digest failed, status: 0x{:X}",
                status);
            return std::make_pair(
                false, std::make_tuple(0, std::vector<uint8_t>{}, 0));
        }

        // Calculate actual digest data size
        size_t numSlots = __builtin_popcount(slotMask);
        size_t totalDigestSize = numSlots * DIGEST_SIZE;
        totalDigestSize = std::min(totalDigestSize, digestBuffer.size());

        LOG_INFO(
            "SPDM Requester: Certificate Digests retrieved successfully - slot_mask: 0x{:02X}, num_slots: {}, total_digest_size: {} bytes",
            slotMask, numSlots, totalDigestSize);

        return std::make_pair(
            true,
            CertificateDigestType{slotMask, digestBuffer, totalDigestSize});
    }

  private:
    std::tuple<std::string, std::vector<uint8_t>> derCertsToPem(
        const std::vector<uint8_t>& derCerts)
    {
        std::string pemChain;
        size_t index = 0;
        size_t currentCertLen = 0;
        std::vector<uint8_t> lastCert;
        while (currentCertLen < derCerts.size())
        {
            const uint8_t* certPtr = nullptr;
            size_t certLen = 0;
            auto ret = libspdm_x509_get_cert_from_cert_chain(
                derCerts.data(), derCerts.size(), index, &certPtr, &certLen);
            if (!ret)
            {
                LOG_DEBUG("No more certificates found in chain at index {}",
                          index);
                break; // No more certs
            }
            lastCert.assign(certPtr, certPtr + certLen);

            std::string base64;
            static const char b64_table[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            size_t i = 0;
            for (; i + 2 < certLen; i += 3)
            {
                uint32_t n = (certPtr[i] << 16) | (certPtr[i + 1] << 8) |
                             certPtr[i + 2];
                base64 += b64_table[(n >> 18) & 63];
                base64 += b64_table[(n >> 12) & 63];
                base64 += b64_table[(n >> 6) & 63];
                base64 += b64_table[n & 63];
            }
            if (i < certLen)
            {
                uint32_t n = certPtr[i] << 16;
                base64 += b64_table[(n >> 18) & 63];
                if (i + 1 < certLen)
                {
                    n |= certPtr[i + 1] << 8;
                    base64 += b64_table[(n >> 12) & 63];
                    base64 += b64_table[(n >> 6) & 63];
                    base64 += '=';
                }
                else
                {
                    base64 += b64_table[(n >> 12) & 63];
                    base64 += "==";
                }
            }
            std::string base64Lines;
            for (size_t j = 0; j < base64.size(); j += 64)
            {
                base64Lines += base64.substr(j, 64) + "\n";
            }
            std::string pem = "-----BEGIN CERTIFICATE-----\n" + base64Lines +
                              "-----END CERTIFICATE-----\n";
            pemChain += pem;
            ++index;
            currentCertLen += certLen;
        }
        return {pemChain, lastCert};
    }

    void* spdmContext_;
    std::shared_ptr<CertificateTrustStore> trustStore_;
    std::unique_ptr<CertificateExchangeRequester> certExchangeRequester_;
};
