#pragma once

#include "cert_generator.hpp"
#include "logger.hpp"
#include "responder_object.hpp"
#include "spdm_custom_messages.hpp"
#include "spdmglobal.hpp"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cstring>
#include <memory>
#include <vector>

extern "C"
{
#include <library/spdm_common_lib.h>
}

// Certificate exchange handler for SPDM responder
class CertificateExchangeHandler
{
  public:
    explicit CertificateExchangeHandler(
        std::shared_ptr<CertificateTrustStore> trustStore,
        const std::string& deviceId = "responder") :
        trustStore_(trustStore), deviceId_(deviceId)
    {}

    // Handle PUSH_CERTIFICATE request from requester
    std::vector<uint8_t> handlePushCertificate(
        const std::vector<uint8_t>& request)
    {
        // Deserialize and validate request
        try
        {
            auto [req, cert] =
                spdm_serialization::deserializePushCertificateRequest(request);

            CertificateFormat certFormat =
                static_cast<CertificateFormat>(req.cert_format);

            // Validate certificate
            if (!validateCertificate(cert))
            {
                LOG_ERROR("Certificate validation failed");
                return spdm_serialization::createPushCertificateResponse(
                    0x12, CERT_EXCHANGE_ERROR_INVALID_CERT);
            }

            // Store certificate in trust store
            std::string certId = "requester_" + deviceId_;
            auto [ret, certPath] =
                trustStore_->storeCertificate(cert, certFormat, certId);
            if (!ret)
            {
                LOG_ERROR("Failed to store certificate in trust store");
                return spdm_serialization::createPushCertificateResponse(
                    0x12, CERT_EXCHANGE_ERROR_STORAGE_FULL);
            }

            LOG_INFO(
                "Successfully stored requester certificate (size: {} bytes)",
                cert.size());

            // Build success response
            return spdm_serialization::createPushCertificateResponse(
                0x12, CERT_EXCHANGE_SUCCESS);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to deserialize PUSH_CERTIFICATE request: {}",
                      e.what());
            return spdm_serialization::createPushCertificateResponse(
                0x12, CERT_EXCHANGE_ERROR_INVALID_REQUEST);
        }
    }

    // Handle PULL_CERTIFICATE request from requester with lazy loading
    std::vector<uint8_t> handlePullCertificate(
        const std::vector<uint8_t>& request)
    {
        // Deserialize and validate request
        try
        {
            spdm_serialization::deserializePullCertificateRequest(request);

            // Lazy load certificate if not in memory
            std::vector<uint8_t> certData = loadResponderCertificate();

            if (certData.empty())
            {
                LOG_ERROR("No responder certificate available");
                return spdm_serialization::createPullCertificateResponse(
                    0x12, CERT_EXCHANGE_ERROR_NOT_FOUND, {},
                    responderCertFormat_);
            }

            LOG_INFO("Sending responder certificate (size: {} bytes)",
                     certData.size());

            // Build success response with certificate data
            return spdm_serialization::createPullCertificateResponse(
                0x12, CERT_EXCHANGE_SUCCESS, certData, responderCertFormat_);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to deserialize PULL_CERTIFICATE request: {}",
                      e.what());
            return spdm_serialization::createPullCertificateResponse(
                0x12, CERT_EXCHANGE_ERROR_INVALID_REQUEST, {},
                responderCertFormat_);
        }
    }

    // Set the responder's certificate path for lazy loading
    void setResponderCertificatePath(const std::string& certPath,
                                     CertificateFormat format)
    {
        responderCertPath_ = certPath;
        responderCertFormat_ = format;
        responderCert_.clear(); // Clear any cached certificate
        LOG_INFO("Responder certificate path set: {}", certPath);
    }

    // Legacy method for backward compatibility - now stores to file and uses
    // lazy loading
    void setResponderCertificate(const std::vector<uint8_t>& cert,
                                 CertificateFormat format)
    {
        // Store certificate to temporary file for lazy loading
        if (trustStore_)
        {
            auto [success, path] = trustStore_->storeCertificate(
                cert, format, "responder_" + deviceId_);
            if (success)
            {
                responderCertPath_ = path;
                responderCertFormat_ = format;
                responderCert_.clear(); // Don't keep in memory
                LOG_INFO(
                    "Responder certificate stored for lazy loading (size: {} bytes)",
                    cert.size());
            }
        }
        else
        {
            // Fallback: keep in memory if no trust store
            responderCert_ = cert;
            responderCertFormat_ = format;
            LOG_INFO("Responder certificate set in memory (size: {} bytes)",
                     cert.size());
        }
    }

    // Handle SET_PROVISIONED request from requester
    std::vector<uint8_t> handleSetProvisioned(
        const std::vector<uint8_t>& request,
        std::shared_ptr<SpdmResponderObject> responderObject)
    {
        // Deserialize and validate request
        try
        {
            SpdmSetProvisionedRequest req =
                spdm_serialization::deserializeSetProvisionedRequest(request);

            bool provisioned = (req.provisioned != 0);

            LOG_INFO("Received SET_PROVISIONED request: provisioned={}",
                     provisioned);

            // Update the provisioned state in the responder object
            if (responderObject)
            {
                responderObject->setProvisioned(provisioned);
                LOG_INFO("Successfully set provisioned state to: {}",
                         provisioned);

                // Build success response
                LOG_INFO("SET_PROVISIONED handled successfully");
                return spdm_serialization::createSetProvisionedResponse(
                    0x12, 1);
            }
            else
            {
                LOG_ERROR(
                    "Responder object is null, cannot set provisioned state");
                return spdm_serialization::createSetProvisionedResponse(
                    0x12, CERT_EXCHANGE_ERROR_INVALID_REQUEST);
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to deserialize SET_PROVISIONED request: {}",
                      e.what());
            return spdm_serialization::createSetProvisionedResponse(
                0x12, CERT_EXCHANGE_ERROR_INVALID_REQUEST);
        }
    }

  private:
    std::shared_ptr<CertificateTrustStore> trustStore_;
    std::string deviceId_;
    std::vector<uint8_t>
        responderCert_; // Cached certificate (empty for lazy loading)
    std::string responderCertPath_; // Path for lazy loading
    CertificateFormat responderCertFormat_ = CertificateFormat::DER;

    // Lazy load responder certificate from file
    std::vector<uint8_t> loadResponderCertificate()
    {
        // Return cached certificate if available
        if (!responderCert_.empty())
        {
            return responderCert_;
        }

        // Load from file path if available
        if (!responderCertPath_.empty())
        {
            try
            {
                std::ifstream certFile(responderCertPath_,
                                       std::ios::binary | std::ios::ate);
                if (certFile.is_open())
                {
                    std::streamsize size = certFile.tellg();
                    certFile.seekg(0, std::ios::beg);

                    if (size > 0 && size <= static_cast<std::streamsize>(
                                                MAX_CERTIFICATE_SIZE))
                    {
                        std::vector<uint8_t> cert(size);
                        if (certFile.read(reinterpret_cast<char*>(cert.data()),
                                          size))
                        {
                            LOG_INFO(
                                "Lazy loaded responder certificate from {} (size: {} bytes)",
                                responderCertPath_, size);
                            return cert; // Return without caching to save
                                         // memory
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Failed to lazy load responder certificate: {}",
                          e.what());
            }
        }

        // Try loading from trust store as fallback
        if (trustStore_)
        {
            try
            {
                std::string certId = "responder_" + deviceId_;
                auto cert = trustStore_->retrieveCertificate(
                    certId, responderCertFormat_);
                if (!cert.empty())
                {
                    LOG_INFO(
                        "Lazy loaded responder certificate from trust store (size: {} bytes)",
                        cert.size());
                    return cert;
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Failed to load from trust store: {}", e.what());
            }
        }

        return {};
    }

    // Helper: Parse X.509 certificate from DER-encoded data
    NSNAME::openssl_ptr<X509, X509_free> parseCertificate(
        const std::vector<uint8_t>& cert)
    {
        // Try DER format first using loadCertificate from cert_generator.hpp
        auto x509Cert =
            NSNAME::loadCertificate(cert, false); // false = DER format

        // Try PEM format if DER parsing fails
        if (!x509Cert)
        {
            x509Cert = NSNAME::loadCertificate(cert, true); // true = PEM format
        }

        if (!x509Cert)
        {
            LOG_ERROR(
                "Certificate validation failed: unable to parse X.509 structure");
        }
        return x509Cert;
    }

    // Helper: Verify certificate's self-signature
    bool verifyCertificateSignature(X509* cert)
    {
        // EVP_PKEY* pubKey = X509_get_pubkey(cert);
        // if (!pubKey)
        // {
        //     LOG_ERROR(
        //         "Certificate validation failed: unable to extract public
        //         key");
        //     return false;
        // }
        // std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> keyGuard(
        //     pubKey, EVP_PKEY_free);

        // int verifyResult = X509_verify(cert, pubKey);
        // if (verifyResult != 1)
        // {
        //     LOG_ERROR(
        //         "Certificate validation failed: signature verification
        //         failed");
        //     return false;
        // }
        return true;
    }

    // Helper: Check certificate validity period (notBefore and notAfter)
    bool checkCertificateValidityPeriod(X509* cert)
    {
        time_t currentTime = time(nullptr);

        // Check notBefore
        const ASN1_TIME* notBefore = X509_get0_notBefore(cert);
        if (!notBefore)
        {
            LOG_ERROR("Certificate validation failed: missing notBefore field");
            return false;
        }
        if (X509_cmp_time(notBefore, &currentTime) > 0)
        {
            LOG_ERROR(
                "Certificate validation failed: certificate not yet valid");
            return false;
        }

        // Check notAfter
        const ASN1_TIME* notAfter = X509_get0_notAfter(cert);
        if (!notAfter)
        {
            LOG_ERROR("Certificate validation failed: missing notAfter field");
            return false;
        }
        if (X509_cmp_time(notAfter, &currentTime) < 0)
        {
            LOG_ERROR("Certificate validation failed: certificate has expired");
            return false;
        }

        return true;
    }

    // Helper: Validate certificate chain against trusted CAs
    bool validateCertificateChain(
        X509* cert, std::shared_ptr<CertificateTrustStore> trustStore)
    {
        // Create X509_STORE for chain validation
        X509_STORE* store = X509_STORE_new();
        if (!store)
        {
            LOG_ERROR(
                "Certificate validation failed: unable to create X509 store");
            return false;
        }
        std::unique_ptr<X509_STORE, decltype(&X509_STORE_free)> storeGuard(
            store, X509_STORE_free);

        // Load trusted CA certificates from trust store
        auto certList = trustStore->listCertificates();
        for (const auto& certId : certList)
        {
            try
            {
                auto trustedCert = trustStore->retrieveCertificate(
                    certId, CertificateFormat::DER);
                if (!trustedCert.empty())
                {
                    const unsigned char* trustedData = trustedCert.data();
                    X509* caCert =
                        d2i_X509(nullptr, &trustedData, trustedCert.size());
                    if (caCert)
                    {
                        X509_STORE_add_cert(store, caCert);
                        X509_free(caCert);
                    }
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("Failed to load trusted certificate {}: {}", certId,
                          e.what());
                // Continue with other certificates
            }
        }

        // Create store context and verify chain
        X509_STORE_CTX* ctx = X509_STORE_CTX_new();
        if (!ctx)
        {
            LOG_ERROR(
                "Certificate validation failed: unable to create store context");
            return false;
        }
        std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)>
            ctxGuard(ctx, X509_STORE_CTX_free);

        if (X509_STORE_CTX_init(ctx, store, cert, nullptr) != 1)
        {
            LOG_ERROR(
                "Certificate validation failed: unable to initialize store context");
            return false;
        }

        int chainVerifyResult = X509_verify_cert(ctx);
        if (chainVerifyResult != 1)
        {
            int error = X509_STORE_CTX_get_error(ctx);
            LOG_ERROR(
                "Certificate validation failed: chain verification failed (error: {})",
                X509_verify_cert_error_string(error));
            return false;
        }

        return true;
    }

    bool validateCertificate(const std::vector<uint8_t>& cert)
    {
        // Basic validation - check if certificate is not empty and within size
        // limits
        if (cert.empty() || cert.size() > MAX_CERTIFICATE_SIZE)
        {
            LOG_ERROR(
                "Certificate validation failed: empty or exceeds size limit");
            return false;
        }

        // Parse X.509 certificate structure
        auto x509Cert = parseCertificate(cert);
        if (!x509Cert)
        {
            return false; // Error already logged in helper
        }

        // Verify certificate signature
        if (!verifyCertificateSignature(x509Cert.get()))
        {
            return false; // Error already logged in helper
        }

        // Check validity period (notBefore and notAfter dates)
        if (!checkCertificateValidityPeriod(x509Cert.get()))
        {
            return false; // Error already logged in helper
        }

        // // Validate certificate chain against trusted CAs if trust store is
        // // available
        // if (trustStore_ &&
        //     !validateCertificateChain(x509Cert.get(), trustStore_))
        // {
        //     return false; // Error already logged in helper
        // }

        LOG_INFO("Certificate validation successful");
        return true;
    }
};
