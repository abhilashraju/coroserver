#pragma once

#include "cert_generator.hpp"
#include "logger.hpp"
#include "spdm_custom_messages.hpp"
#include "spdmglobal.hpp"

#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <vector>

extern "C"
{
#include <library/spdm_common_lib.h>
}

// Forward declarations for send/receive function types
using SendMessageFunc = std::function<bool(const std::vector<uint8_t>&)>;
using ReceiveMessageFunc =
    std::function<std::pair<bool, std::vector<uint8_t>>()>;

// Certificate exchange functionality for SPDM requester
class CertificateExchangeRequester
{
  public:
    explicit CertificateExchangeRequester(
        void* spdmContext, std::shared_ptr<CertificateTrustStore> trustStore,
        const std::string& deviceId = "requester") :
        spdmContext_(spdmContext), trustStore_(trustStore), deviceId_(deviceId)
    {}

    // Set custom send/receive functions
    void setSendFunction(SendMessageFunc func)
    {
        sendFunc_ = std::move(func);
    }
    void setReceiveFunction(ReceiveMessageFunc func)
    {
        receiveFunc_ = std::move(func);
    }

    // Push requester's certificate to responder
    bool pushCertificateToResponder(const std::vector<uint8_t>& cert,
                                    CertificateFormat format)
    {
        if (cert.empty() || cert.size() > MAX_CERTIFICATE_SIZE)
        {
            LOG_ERROR("Invalid certificate size: {}", cert.size());
            return false;
        }

        // Validate certificate structure before sending
        if (!validateCertificateStructure(cert, format))
        {
            LOG_ERROR("Certificate validation failed - invalid structure");
            return false;
        }

        // Build PUSH_CERTIFICATE request using serialization
        std::vector<uint8_t> request =
            spdm_serialization::createPushCertificateRequest(0x12, cert,
                                                             format);

        LOG_INFO("Sending PUSH_CERTIFICATE request (cert size: {} bytes)",
                 cert.size());

        // Send request
        if (!sendMessage(request))
        {
            LOG_ERROR("Failed to send PUSH_CERTIFICATE request");
            return false;
        }

        // Receive response
        auto [success, response] = receiveMessage();
        if (!success || response.size() < sizeof(SpdmPushCertificateResponse))
        {
            LOG_ERROR("Failed to receive PUSH_CERTIFICATE response");
            return false;
        }

        // Deserialize response
        try
        {
            SpdmPushCertificateResponse resp =
                spdm_serialization::deserializePushCertificateResponse(
                    response);

            if (resp.status != CERT_EXCHANGE_SUCCESS)
            {
                LOG_ERROR("PUSH_CERTIFICATE failed with status: {}",
                          resp.status);
                return false;
            }

            LOG_INFO("PUSH_CERTIFICATE successful");
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to deserialize PUSH_CERTIFICATE response: {}",
                      e.what());
            return false;
        }
    }

    // Pull responder's certificate
    std::pair<bool, std::vector<uint8_t>> pullCertificateFromResponder(
        CertificateFormat format)
    {
        std::vector<uint8_t> cert;

        // Build PULL_CERTIFICATE request using serialization
        std::vector<uint8_t> request =
            spdm_serialization::createPullCertificateRequest(0x12, format);

        LOG_INFO("Sending PULL_CERTIFICATE request");

        // Send request
        if (!sendMessage(request))
        {
            LOG_ERROR("Failed to send PULL_CERTIFICATE request");
            return {false, cert};
        }

        // Receive response
        auto [success, response] = receiveMessage();
        if (!success || response.size() < sizeof(SpdmPullCertificateResponse))
        {
            LOG_ERROR("Failed to receive PULL_CERTIFICATE response");
            return {false, cert};
        }

        // Deserialize response
        try
        {
            auto [resp, certData] =
                spdm_serialization::deserializePullCertificateResponse(
                    response);

            if (resp.status != CERT_EXCHANGE_SUCCESS)
            {
                LOG_ERROR("PULL_CERTIFICATE failed with status: {}",
                          resp.status);
                return {false, cert};
            }

            cert = std::move(certData);

            LOG_INFO("PULL_CERTIFICATE successful (cert size: {} bytes)",
                     cert.size());

            // Store responder certificate in trust store
            std::string certId = "responder_" + deviceId_;
            auto [saved, certPath] =
                trustStore_->storeCertificate(cert, format, certId);
            if (saved)
            {
                LOG_INFO("Stored responder certificate in trust store at: {}",
                         certPath);
                lastStoredCertPath_ = certPath;
            }
            else
            {
                LOG_ERROR(
                    "Failed to store responder certificate in trust store");
            }

            return {true, cert};
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to deserialize PULL_CERTIFICATE response: {}",
                      e.what());
            return {false, cert};
        }
    }

    // Send SET_PROVISIONED request to responder
    bool setProvisionedState(bool provisioned)
    {
        // Build SET_PROVISIONED request using serialization
        std::vector<uint8_t> request =
            spdm_serialization::createSetProvisionedRequest(0x12, provisioned);

        LOG_INFO("Sending SET_PROVISIONED request (provisioned={})",
                 provisioned);

        // Send request
        if (!sendMessage(request))
        {
            LOG_ERROR("Failed to send SET_PROVISIONED request");
            return false;
        }

        // Receive response
        auto [success, response] = receiveMessage();
        if (!success || response.size() < sizeof(SpdmSetProvisionedResponse))
        {
            LOG_ERROR("Failed to receive SET_PROVISIONED response");
            return false;
        }

        // Deserialize response
        try
        {
            SpdmSetProvisionedResponse resp =
                spdm_serialization::deserializeSetProvisionedResponse(response);

            if (resp.status != CERT_EXCHANGE_SUCCESS && resp.status != 1)
            {
                LOG_ERROR("SET_PROVISIONED failed with status: {}",
                          resp.status);
                return false;
            }

            LOG_INFO("SET_PROVISIONED successful");
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to deserialize SET_PROVISIONED response: {}",
                      e.what());
            return false;
        }
    }

    // Load requester certificate from file using cert_generator.hpp APIs
    bool loadRequesterCertificate(const std::string& certPath,
                                  CertificateFormat format)
    {
        // Use loadCertificate from cert_generator.hpp to load and validate
        bool isPem = (format == CertificateFormat::PEM);
        auto x509Cert = NSNAME::loadCertificate(certPath, isPem);

        if (!x509Cert)
        {
            LOG_ERROR("Failed to load certificate from file: {}", certPath);
            return false;
        }

        // Convert X509 certificate back to buffer format for storage
        // This call validates the certificate and converts it to BIO format
        auto certBio = NSNAME::certificateToBio(x509Cert, isPem);
        if (!certBio)
        {
            LOG_ERROR(
                "Failed to convert certificate to buffer format for file: {}. "
                "Certificate may be invalid or conversion failed.",
                certPath);
            return false;
        }

        // Read certificate data from BIO
        BUF_MEM* bioMem = nullptr;
        BIO_get_mem_ptr(certBio.get(), &bioMem);
        if (!bioMem || !bioMem->data || bioMem->length == 0)
        {
            LOG_ERROR("Failed to get certificate data from BIO");
            return false;
        }

        if (bioMem->length > MAX_CERTIFICATE_SIZE)
        {
            LOG_ERROR("Certificate size exceeds maximum: {}", bioMem->length);
            return false;
        }

        requesterCert_.assign(
            reinterpret_cast<uint8_t*>(bioMem->data),
            reinterpret_cast<uint8_t*>(bioMem->data) + bioMem->length);
        requesterCertFormat_ = format;

        LOG_INFO("Loaded requester certificate from {} (size: {} bytes)",
                 certPath, requesterCert_.size());

        return true;
    }

    // Perform full certificate exchange (push then pull)
    bool performCertificateExchange()
    {
        if (requesterCert_.empty())
        {
            LOG_ERROR("No requester certificate loaded");
            return false;
        }

        LOG_INFO("Starting certificate exchange...");

        // Step 1: Push requester certificate to responder
        if (!pushCertificateToResponder(requesterCert_, requesterCertFormat_))
        {
            LOG_ERROR("Failed to push certificate to responder");
            return false;
        }

        // Step 2: Pull responder certificate
        auto [success, responderCert] =
            pullCertificateFromResponder(requesterCertFormat_);

        if (!success)
        {
            LOG_ERROR("Failed to pull certificate from responder");
            return false;
        }

        LOG_INFO("Certificate exchange completed successfully");
        return true;
    }

    // Get the path of the last stored certificate
    std::string getStoredCertificatePath() const
    {
        return lastStoredCertPath_;
    }

  private:
    static constexpr size_t MAX_CERTIFICATE_SIZE = 8192;

    // Validate certificate structure using OpenSSL smart pointers
    bool validateCertificateStructure(const std::vector<uint8_t>& cert,
                                      CertificateFormat format) const
    {
        if (cert.empty())
        {
            return false;
        }

        // Create BIO from certificate data using smart pointer
        NSNAME::BIOPtr bio = NSNAME::makeBIOPtr(
            BIO_new_mem_buf(cert.data(), static_cast<int>(cert.size())));
        if (!bio)
        {
            return false;
        }

        // Try to parse certificate based on format
        NSNAME::X509Ptr x509 = NSNAME::makeX509Ptr(nullptr);
        if (format == CertificateFormat::DER)
        {
            x509 = NSNAME::makeX509Ptr(d2i_X509_bio(bio.get(), nullptr));
        }
        else // PEM format
        {
            x509 = NSNAME::makeX509Ptr(
                PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
        }

        // If parsing succeeded, certificate structure is valid
        return (x509 != nullptr);
    }

    void* spdmContext_;
    std::shared_ptr<CertificateTrustStore> trustStore_;
    std::string deviceId_;
    std::vector<uint8_t> requesterCert_;
    CertificateFormat requesterCertFormat_ = CertificateFormat::DER;
    SendMessageFunc sendFunc_;
    ReceiveMessageFunc receiveFunc_;
    std::string lastStoredCertPath_;

    // Send message helper
    bool sendMessage(const std::vector<uint8_t>& message)
    {
        if (!sendFunc_)
        {
            LOG_ERROR("Send function not set");
            return false;
        }

        return sendFunc_(message);
    }

    // Receive message helper
    std::pair<bool, std::vector<uint8_t>> receiveMessage()
    {
        if (!receiveFunc_)
        {
            LOG_ERROR("Receive function not set");
            return {false, std::vector<uint8_t>{}};
        }

        return receiveFunc_();
    }
};
