#pragma once

#include <openssl/pem.h>
#include <openssl/x509.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C"
{
#include <library/spdm_common_lib.h>
}

// RAII wrappers for OpenSSL types
template <typename T, void (*Deleter)(T*)>
using openssl_ptr = std::unique_ptr<T, decltype(Deleter)>;

using X509Ptr = openssl_ptr<X509, X509_free>;
using BIOPtr = openssl_ptr<BIO, BIO_free_all>;

inline X509Ptr makeX509Ptr(X509* ptr)
{
    return X509Ptr(ptr, X509_free);
}

inline BIOPtr makeBIOPtr(BIO* ptr)
{
    return BIOPtr(ptr, BIO_free_all);
}
// Custom SPDM message codes for certificate exchange
// Using vendor-defined message codes (0x7E-0x7F range for vendor-specific)
constexpr uint8_t SPDM_PUSH_CERTIFICATE = 0x7E;
constexpr uint8_t SPDM_PULL_CERTIFICATE = 0x7F;
constexpr uint8_t SPDM_SET_PROVISIONED = 0x7D;

// Response codes
constexpr uint8_t SPDM_PUSH_CERTIFICATE_ACK = 0x7E;
constexpr uint8_t SPDM_PULL_CERTIFICATE_ACK = 0x7F;
constexpr uint8_t SPDM_SET_PROVISIONED_ACK = 0x7D;

// Status codes for certificate exchange
constexpr uint8_t CERT_EXCHANGE_SUCCESS = 0x00;
constexpr uint8_t CERT_EXCHANGE_ERROR_INVALID_CERT = 0x01;
constexpr uint8_t CERT_EXCHANGE_ERROR_STORAGE_FULL = 0x02;
constexpr uint8_t CERT_EXCHANGE_ERROR_NOT_FOUND = 0x03;
constexpr uint8_t CERT_EXCHANGE_ERROR_INVALID_REQUEST = 0x04;

// Maximum certificate size (256KB - reasonable for X.509 certificate chains)
// Typical certificates are 1-10KB, chains with intermediates rarely exceed 64KB
constexpr size_t MAX_CERTIFICATE_SIZE = 256 * 1024;

// SPDM message header structure
#pragma pack(1)
// struct SpdmMessageHeader
// {
//     uint8_t spdm_version;
//     uint8_t request_response_code;
//     uint8_t param1;
//     uint8_t param2;
// };
using SpdmMessageHeader = spdm_message_header_t;
// PUSH_CERTIFICATE request structure
struct SpdmPushCertificateRequest
{
    SpdmMessageHeader header;
    uint32_t cert_size;
    uint8_t cert_format; // 0 = DER, 1 = PEM
    uint8_t reserved[3];
    // Followed by certificate data
};

// PUSH_CERTIFICATE response structure
struct SpdmPushCertificateResponse
{
    SpdmMessageHeader header;
    uint8_t status;
    uint8_t reserved[3];
};

// PULL_CERTIFICATE request structure
struct SpdmPullCertificateRequest
{
    SpdmMessageHeader header;
    uint8_t cert_format; // 0 = DER, 1 = PEM
    uint8_t reserved[3];
};

// PULL_CERTIFICATE response structure
struct SpdmPullCertificateResponse
{
    SpdmMessageHeader header;
    uint8_t status;
    uint32_t cert_size;
    uint8_t cert_format;
    uint8_t reserved[2];
    // Followed by certificate data
};

// SET_PROVISIONED request structure
struct SpdmSetProvisionedRequest
{
    SpdmMessageHeader header;
    uint8_t provisioned; // 0 = false, 1 = true
    uint8_t reserved[3];
};

// SET_PROVISIONED response structure
struct SpdmSetProvisionedResponse
{
    SpdmMessageHeader header;
    uint8_t status;
    uint8_t reserved[3];
};
#pragma pack()

// Certificate format enumeration
enum class CertificateFormat : uint8_t
{
    DER = 0,
    PEM = 1
};

// Certificate trust store interface
class CertificateTrustStore
{
  public:
    virtual ~CertificateTrustStore() = default;

    // Store a certificate in the trust store
    virtual std::tuple<bool, std::string> storeCertificate(
        const std::vector<uint8_t>& cert, CertificateFormat format,
        const std::string& identifier) = 0;

    // Retrieve a certificate from the trust store
    virtual std::vector<uint8_t> retrieveCertificate(
        const std::string& identifier, CertificateFormat format) = 0;

    // Check if a certificate exists
    virtual bool hasCertificate(const std::string& identifier) = 0;

    // List all certificates
    virtual std::vector<std::string> listCertificates() = 0;

    // Remove a certificate
    virtual bool removeCertificate(const std::string& identifier) = 0;

    // Get the file path of a stored certificate
    virtual std::string getCertificatePath(const std::string& identifier) = 0;
};
namespace fs = std::filesystem;
// File-based certificate trust store implementation
class FileCertificateTrustStore : public CertificateTrustStore
{
  public:
    explicit FileCertificateTrustStore(const std::string& storePath) :
        storePath_(storePath)
    {
        // Create the trust store directory if it doesn't exist
        try
        {
            if (!fs::exists(storePath_))
            {
                fs::create_directories(storePath_);
            }
        }
        catch (const fs::filesystem_error& e)
        {
            LOG_ERROR("Error creating trust store directory:{} {}", storePath_,
                      e.what());
            throw;
        }
    }

  private:
    // Helper function to create certificate hash symlink for trust store
    bool createCertificateHashFile(const X509Ptr& cert,
                                   const std::string& certPath)
    {
        if (!cert)
        {
            LOG_ERROR("Certificate pointer is null");
            return false;
        }

        // Calculate the certificate hash using X509_subject_name_hash
        unsigned long hash = X509_subject_name_hash(cert.get());

        // Get the directory path where the certificate is stored
        std::filesystem::path certFilePath(certPath);
        std::filesystem::path certDir = certFilePath.parent_path();

        // Format hash as 8-digit hex string
        std::ostringstream hashStream;
        hashStream << std::hex << std::setw(8) << std::setfill('0') << hash;
        std::string hashStr = hashStream.str();

        // Find the next available index for this hash (handle collisions)
        int index = 0;
        std::filesystem::path hashFilePath;
        do
        {
            hashFilePath = certDir / (hashStr + "." + std::to_string(index));
            index++;
        } while (std::filesystem::exists(hashFilePath) && index < 100);

        if (index >= 100)
        {
            LOG_ERROR("Too many hash collisions for certificate hash {}",
                      hashStr);
            return false;
        }

        // Create symlink from hash file to actual certificate
        std::error_code ec;
        std::filesystem::create_symlink(certFilePath.filename(), hashFilePath,
                                        ec);
        if (ec)
        {
            LOG_ERROR("Failed to create hash symlink {} -> {}: {}",
                      hashFilePath.string(), certFilePath.string(),
                      ec.message());
            return false;
        }

        LOG_DEBUG("Created certificate hash file: {} -> {}",
                  hashFilePath.string(), certFilePath.string());
        return true;
    }

    // Helper function to parse certificate from raw data
    X509Ptr parseCertificate(const std::vector<uint8_t>& cert,
                             CertificateFormat format)
    {
        BIOPtr bio = makeBIOPtr(
            BIO_new_mem_buf(cert.data(), static_cast<int>(cert.size())));
        if (!bio)
        {
            LOG_ERROR("Failed to create BIO from certificate data");
            return makeX509Ptr(nullptr);
        }

        X509* x509 = nullptr;
        if (format == CertificateFormat::PEM)
        {
            x509 = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
        }
        else // DER format
        {
            x509 = d2i_X509_bio(bio.get(), nullptr);
        }

        if (!x509)
        {
            LOG_ERROR("Failed to parse certificate from {} format",
                      format == CertificateFormat::PEM ? "PEM" : "DER");
        }

        return makeX509Ptr(x509);
    }

  public:
    std::tuple<bool, std::string> storeCertificate(
        const std::vector<uint8_t>& cert, CertificateFormat format,
        const std::string& identifier) override
    {
        if (cert.empty() || identifier.empty())
        {
            return std::make_tuple(false, std::string());
        }

        if (cert.size() > MAX_CERTIFICATE_SIZE)
        {
            LOG_ERROR("Certificate size exceeds maximum: {}", cert.size());
            return std::make_tuple(false, std::string());
        }

        try
        {
            std::string certPath = getCertificatePath(identifier, format);
            std::ofstream outFile(certPath, std::ios::binary);

            if (!outFile)
            {
                LOG_ERROR("Failed to open certificate file: {}", certPath);
                return std::make_tuple(false, std::string());
            }

            outFile.write(reinterpret_cast<const char*>(cert.data()),
                          cert.size());
            outFile.close();

            if (!outFile)
            {
                LOG_ERROR("Failed to write certificate file: {}", certPath);
                return std::make_tuple(false, std::string());
            }

            // Parse the certificate to create hash-based symlink
            X509Ptr x509 = parseCertificate(cert, format);
            if (x509)
            {
                // Create hash-based symlink for trust store compatibility
                if (!createCertificateHashFile(x509, certPath))
                {
                    LOG_ERROR("Failed to create certificate hash file for {}",
                              certPath);
                    // Continue anyway - the certificate is stored, just without
                    // hash link
                }
                else
                {
                    LOG_DEBUG("Certificate stored with hash symlink: {}",
                              certPath);
                }
            }
            else
            {
                LOG_WARNING("Could not parse certificate for hash generation, "
                            "stored without hash symlink: {}",
                            certPath);
            }

            return std::make_tuple(true, certPath);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception while storing certificate: {}", e.what());
            return std::make_tuple(false, std::string());
        }
    }

    std::vector<uint8_t> retrieveCertificate(const std::string& identifier,
                                             CertificateFormat format) override
    {
        if (identifier.empty())
        {
            return {};
        }

        try
        {
            std::string certPath = getCertificatePath(identifier, format);

            if (!fs::exists(certPath))
            {
                LOG_ERROR("Certificate file not found: {}", certPath);
                return {};
            }

            std::uintmax_t size = fs::file_size(certPath);

            if (size > MAX_CERTIFICATE_SIZE)
            {
                LOG_ERROR("Certificate size exceeds maximum: {}", size);
                return {};
            }

            std::ifstream inFile(certPath, std::ios::binary);

            if (!inFile)
            {
                LOG_ERROR("Failed to open certificate file: {}", certPath);
                return {};
            }

            std::vector<uint8_t> cert(size);
            if (!inFile.read(reinterpret_cast<char*>(cert.data()), size))
            {
                LOG_ERROR("Failed to read certificate file: {}", certPath);
                return {};
            }

            return cert;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception while retrieving certificate: {}", e.what());
            return {};
        }
    }

    bool hasCertificate(const std::string& identifier) override
    {
        if (identifier.empty())
        {
            return false;
        }

        // Check for both PEM and DER formats
        std::string pemPath =
            getCertificatePath(identifier, CertificateFormat::PEM);
        std::string derPath =
            getCertificatePath(identifier, CertificateFormat::DER);

        return fs::exists(pemPath) || fs::exists(derPath);
    }

    std::vector<std::string> listCertificates() override
    {
        std::vector<std::string> certificates;

        try
        {
            if (!fs::exists(storePath_))
            {
                return certificates;
            }

            for (const auto& entry : fs::directory_iterator(storePath_))
            {
                if (entry.is_regular_file())
                {
                    std::string filename = entry.path().stem().string();

                    // Avoid duplicates (same cert in both PEM and DER format)
                    if (std::find(certificates.begin(), certificates.end(),
                                  filename) == certificates.end())
                    {
                        certificates.push_back(filename);
                    }
                }
            }
        }
        catch (const fs::filesystem_error& e)
        {
            LOG_ERROR("Error listing certificates: {}", e.what());
        }

        return certificates;
    }

    bool removeCertificate(const std::string& identifier) override
    {
        if (identifier.empty())
        {
            return false;
        }

        bool removed = false;

        try
        {
            // Try to remove both PEM and DER versions
            std::string pemPath =
                getCertificatePath(identifier, CertificateFormat::PEM);
            std::string derPath =
                getCertificatePath(identifier, CertificateFormat::DER);

            if (fs::exists(pemPath))
            {
                fs::remove(pemPath);
                removed = true;
            }

            if (fs::exists(derPath))
            {
                fs::remove(derPath);
                removed = true;
            }

            return removed;
        }
        catch (const fs::filesystem_error& e)
        {
            LOG_ERROR("Error removing certificate: {}", e.what());
            return false;
        }
    }

    std::string getCertificatePath(const std::string& identifier) override
    {
        // Default to PEM format for the public interface
        return getCertificatePath(identifier, CertificateFormat::PEM);
    }

  private:
    std::string storePath_;
    std::string getCertificatePath(const std::string& identifier,
                                   CertificateFormat format) const
    {
        std::string extension =
            (format == CertificateFormat::PEM) ? ".pem" : ".der";
        return storePath_ + "/" + identifier + extension;
    }
};

// ============================================================================
// Serialization/Deserialization Utilities
// ============================================================================

namespace spdm_serialization
{

// Helper function to serialize POD types to byte vector
template <typename T>
inline void serializePOD(std::vector<uint8_t>& buffer, const T& data)
{
    static_assert(std::is_trivially_copyable_v<T>,
                  "Type must be trivially copyable");
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&data);
    buffer.insert(buffer.end(), ptr, ptr + sizeof(T));
}

// Helper function to deserialize POD types from byte span
template <typename T>
inline T deserializePOD(std::span<const uint8_t> data, size_t& offset)
{
    static_assert(std::is_trivially_copyable_v<T>,
                  "Type must be trivially copyable");
    if (offset + sizeof(T) > data.size())
    {
        throw std::runtime_error("Buffer underflow during deserialization");
    }
    T result;
    std::memcpy(&result, data.data() + offset, sizeof(T));
    offset += sizeof(T);
    return result;
}

// Helper function to deserialize POD types from byte vector
template <typename T>
inline T deserializePOD(const std::vector<uint8_t>& data, size_t& offset)
{
    return deserializePOD<T>(std::span<const uint8_t>(data), offset);
}

// ============================================================================
// PUSH_CERTIFICATE Request Serialization
// ============================================================================

inline std::vector<uint8_t> serializePushCertificateRequest(
    const SpdmPushCertificateRequest& request,
    const std::vector<uint8_t>& certData)
{
    // Validate that certData.size() matches request.cert_size
    if (certData.size() != request.cert_size)
    {
        throw std::runtime_error(
            "Certificate data size mismatch: certData.size() != request.cert_size");
    }

    std::vector<uint8_t> buffer;
    buffer.reserve(sizeof(SpdmPushCertificateRequest) + certData.size());

    // Serialize the header and request structure
    serializePOD(buffer, request);

    // Append certificate data
    buffer.insert(buffer.end(), certData.begin(), certData.end());

    return buffer;
}

inline std::tuple<SpdmPushCertificateRequest, std::vector<uint8_t>>
    deserializePushCertificateRequest(const std::vector<uint8_t>& data)
{
    size_t offset = 0;

    if (data.size() < sizeof(SpdmPushCertificateRequest))
    {
        throw std::runtime_error(
            "Invalid PUSH_CERTIFICATE request: buffer too small");
    }

    // Deserialize the request structure
    SpdmPushCertificateRequest request =
        deserializePOD<SpdmPushCertificateRequest>(data, offset);

    // Validate cert_size
    if (request.cert_size > MAX_CERTIFICATE_SIZE)
    {
        throw std::runtime_error(
            "Invalid PUSH_CERTIFICATE request: cert_size exceeds maximum");
    }

    if (offset + request.cert_size > data.size())
    {
        throw std::runtime_error(
            "Invalid PUSH_CERTIFICATE request: insufficient certificate data");
    }

    // Extract certificate data
    std::vector<uint8_t> certData(data.begin() + offset,
                                  data.begin() + offset + request.cert_size);

    return {request, certData};
}

// ============================================================================
// PUSH_CERTIFICATE Response Serialization
// ============================================================================

inline std::vector<uint8_t> serializePushCertificateResponse(
    const SpdmPushCertificateResponse& response)
{
    std::vector<uint8_t> buffer;
    buffer.reserve(sizeof(SpdmPushCertificateResponse));
    serializePOD(buffer, response);
    return buffer;
}

inline SpdmPushCertificateResponse deserializePushCertificateResponse(
    const std::vector<uint8_t>& data)
{
    size_t offset = 0;

    if (data.size() < sizeof(SpdmPushCertificateResponse))
    {
        throw std::runtime_error(
            "Invalid PUSH_CERTIFICATE response: buffer too small");
    }

    return deserializePOD<SpdmPushCertificateResponse>(data, offset);
}

// ============================================================================
// PULL_CERTIFICATE Request Serialization
// ============================================================================

inline std::vector<uint8_t> serializePullCertificateRequest(
    const SpdmPullCertificateRequest& request)
{
    std::vector<uint8_t> buffer;
    buffer.reserve(sizeof(SpdmPullCertificateRequest));
    serializePOD(buffer, request);
    return buffer;
}

inline SpdmPullCertificateRequest deserializePullCertificateRequest(
    const std::vector<uint8_t>& data)
{
    size_t offset = 0;

    if (data.size() < sizeof(SpdmPullCertificateRequest))
    {
        throw std::runtime_error(
            "Invalid PULL_CERTIFICATE request: buffer too small");
    }

    return deserializePOD<SpdmPullCertificateRequest>(data, offset);
}

// ============================================================================
// PULL_CERTIFICATE Response Serialization
// ============================================================================

inline std::vector<uint8_t> serializePullCertificateResponse(
    const SpdmPullCertificateResponse& response,
    const std::vector<uint8_t>& certData)
{
    // Validate that certData.size() matches response.cert_size
    if (certData.size() != response.cert_size)
    {
        throw std::runtime_error(
            "Certificate data size mismatch: certData.size() != response.cert_size");
    }

    std::vector<uint8_t> buffer;
    buffer.reserve(sizeof(SpdmPullCertificateResponse) + certData.size());

    // Serialize the header and response structure
    serializePOD(buffer, response);

    // Append certificate data
    buffer.insert(buffer.end(), certData.begin(), certData.end());

    return buffer;
}

inline std::tuple<SpdmPullCertificateResponse, std::vector<uint8_t>>
    deserializePullCertificateResponse(const std::vector<uint8_t>& data)
{
    size_t offset = 0;

    if (data.size() < sizeof(SpdmPullCertificateResponse))
    {
        throw std::runtime_error(
            "Invalid PULL_CERTIFICATE response: buffer too small");
    }

    // Deserialize the response structure
    SpdmPullCertificateResponse response =
        deserializePOD<SpdmPullCertificateResponse>(data, offset);

    // Validate cert_size
    if (response.cert_size > MAX_CERTIFICATE_SIZE)
    {
        throw std::runtime_error(
            "Invalid PULL_CERTIFICATE response: cert_size exceeds maximum");
    }

    if (offset + response.cert_size > data.size())
    {
        throw std::runtime_error(
            "Invalid PULL_CERTIFICATE response: insufficient certificate data");
    }

    // Extract certificate data
    std::vector<uint8_t> certData(data.begin() + offset,
                                  data.begin() + offset + response.cert_size);

    return {response, certData};
}

// ============================================================================
// SET_PROVISIONED Request Serialization
// ============================================================================

inline std::vector<uint8_t> serializeSetProvisionedRequest(
    const SpdmSetProvisionedRequest& request)
{
    std::vector<uint8_t> buffer;
    buffer.reserve(sizeof(SpdmSetProvisionedRequest));
    serializePOD(buffer, request);
    return buffer;
}

inline SpdmSetProvisionedRequest deserializeSetProvisionedRequest(
    const std::vector<uint8_t>& data)
{
    size_t offset = 0;

    if (data.size() < sizeof(SpdmSetProvisionedRequest))
    {
        throw std::runtime_error(
            "Invalid SET_PROVISIONED request: buffer too small");
    }

    return deserializePOD<SpdmSetProvisionedRequest>(data, offset);
}

// ============================================================================
// SET_PROVISIONED Response Serialization
// ============================================================================

inline std::vector<uint8_t> serializeSetProvisionedResponse(
    const SpdmSetProvisionedResponse& response)
{
    std::vector<uint8_t> buffer;
    buffer.reserve(sizeof(SpdmSetProvisionedResponse));
    serializePOD(buffer, response);
    return buffer;
}

inline SpdmSetProvisionedResponse deserializeSetProvisionedResponse(
    const std::vector<uint8_t>& data)
{
    size_t offset = 0;

    if (data.size() < sizeof(SpdmSetProvisionedResponse))
    {
        throw std::runtime_error(
            "Invalid SET_PROVISIONED response: buffer too small");
    }

    return deserializePOD<SpdmSetProvisionedResponse>(data, offset);
}

// ============================================================================
// Helper Functions for Creating Messages
// ============================================================================

// Create a PUSH_CERTIFICATE request with certificate data
inline std::vector<uint8_t> createPushCertificateRequest(
    uint8_t spdmVersion, const std::vector<uint8_t>& certData,
    CertificateFormat format)
{
    SpdmPushCertificateRequest request{};
    request.header.spdm_version = spdmVersion;
    request.header.request_response_code = SPDM_PUSH_CERTIFICATE;
    request.header.param1 = 0;
    request.header.param2 = 0;
    request.cert_size = static_cast<uint32_t>(certData.size());
    request.cert_format = static_cast<uint8_t>(format);
    std::memset(request.reserved, 0, sizeof(request.reserved));

    return serializePushCertificateRequest(request, certData);
}

// Create a PUSH_CERTIFICATE response
inline std::vector<uint8_t> createPushCertificateResponse(uint8_t spdmVersion,
                                                          uint8_t status)
{
    SpdmPushCertificateResponse response{};
    response.header.spdm_version = spdmVersion;
    response.header.request_response_code = SPDM_PUSH_CERTIFICATE_ACK;
    response.header.param1 = 0;
    response.header.param2 = 0;
    response.status = status;
    std::memset(response.reserved, 0, sizeof(response.reserved));

    return serializePushCertificateResponse(response);
}

// Create a PULL_CERTIFICATE request
inline std::vector<uint8_t> createPullCertificateRequest(
    uint8_t spdmVersion, CertificateFormat format)
{
    SpdmPullCertificateRequest request{};
    request.header.spdm_version = spdmVersion;
    request.header.request_response_code = SPDM_PULL_CERTIFICATE;
    request.header.param1 = 0;
    request.header.param2 = 0;
    request.cert_format = static_cast<uint8_t>(format);
    std::memset(request.reserved, 0, sizeof(request.reserved));

    return serializePullCertificateRequest(request);
}

// Create a PULL_CERTIFICATE response with certificate data
inline std::vector<uint8_t> createPullCertificateResponse(
    uint8_t spdmVersion, uint8_t status, const std::vector<uint8_t>& certData,
    CertificateFormat format)
{
    SpdmPullCertificateResponse response{};
    response.header.spdm_version = spdmVersion;
    response.header.request_response_code = SPDM_PULL_CERTIFICATE_ACK;
    response.header.param1 = 0;
    response.header.param2 = 0;
    response.status = status;
    response.cert_size = static_cast<uint32_t>(certData.size());
    response.cert_format = static_cast<uint8_t>(format);
    std::memset(response.reserved, 0, sizeof(response.reserved));

    return serializePullCertificateResponse(response, certData);
}

// Create a SET_PROVISIONED request
inline std::vector<uint8_t> createSetProvisionedRequest(uint8_t spdmVersion,
                                                        bool provisioned)
{
    SpdmSetProvisionedRequest request{};
    request.header.spdm_version = spdmVersion;
    request.header.request_response_code = SPDM_SET_PROVISIONED;
    request.header.param1 = 0;
    request.header.param2 = 0;
    request.provisioned = provisioned ? 1 : 0;
    std::memset(request.reserved, 0, sizeof(request.reserved));

    return serializeSetProvisionedRequest(request);
}

// Create a SET_PROVISIONED response
inline std::vector<uint8_t> createSetProvisionedResponse(uint8_t spdmVersion,
                                                         uint8_t status)
{
    SpdmSetProvisionedResponse response{};
    response.header.spdm_version = spdmVersion;
    response.header.request_response_code = SPDM_SET_PROVISIONED_ACK;
    response.header.param1 = 0;
    response.header.param2 = 0;
    response.status = status;
    std::memset(response.reserved, 0, sizeof(response.reserved));

    return serializeSetProvisionedResponse(response);
}

} // namespace spdm_serialization
