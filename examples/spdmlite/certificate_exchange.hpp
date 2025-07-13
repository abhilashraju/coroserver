#pragma once
#include "eventmethods.hpp"
#include "eventqueue.hpp"
#include "globaldefs.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <memory>
#include <string>

constexpr auto INSTALL_CERTIFICATES = "InstallCertificates";
constexpr auto INSTALL_CERTIFICATES_RESP = "InstallCertificatesResp";

// Helper for unique_ptr with OpenSSL types
template <typename T, void (*Deleter)(T*)>
using openssl_ptr = std::unique_ptr<T, decltype(Deleter)>;
using X509Ptr = openssl_ptr<X509, X509_free>;
X509Ptr makeX509Ptr(X509* ptr)
{
    return X509Ptr(ptr, X509_free);
}
using EVP_PKEYPtr = openssl_ptr<EVP_PKEY, EVP_PKEY_free>;
EVP_PKEYPtr makeEVPPKeyPtr(EVP_PKEY* ptr)
{
    return EVP_PKEYPtr(ptr, EVP_PKEY_free);
}

using EVP_PKEYPtr = openssl_ptr<EVP_PKEY, EVP_PKEY_free>;

// Convert PEM certificate file to DER format and save to file
bool pemCertToDer(const std::string& pemPath, const std::string& derPath)
{
    FILE* pemFile = fopen(pemPath.c_str(), "r");
    if (!pemFile)
        return false;
    X509Ptr cert =
        makeX509Ptr(PEM_read_X509(pemFile, nullptr, nullptr, nullptr));
    fclose(pemFile);
    if (!cert)
        return false;

    FILE* derFile = fopen(derPath.c_str(), "wb");
    if (!derFile)
    {
        return false;
    }
    int ret = i2d_X509_fp(derFile, cert.get());
    fclose(derFile);
    return ret == 1;
}

// Convert DER certificate file to PEM format and save to file
bool derCertToPem(const std::string& derPath, const std::string& pemPath)
{
    FILE* derFile = fopen(derPath.c_str(), "rb");
    if (!derFile)
        return false;
    X509Ptr cert = makeX509Ptr(d2i_X509_fp(derFile, nullptr));
    fclose(derFile);
    if (!cert)
        return false;

    FILE* pemFile = fopen(pemPath.c_str(), "w");
    if (!pemFile)
    {
        return false;
    }
    int ret = PEM_write_X509(pemFile, cert.get());
    fclose(pemFile);
    return ret == 1;
}

// Convert PEM private key file to DER format and save to file
bool pemKeyToDer(const std::string& pemPath, const std::string& derPath)
{
    FILE* pemFile = fopen(pemPath.c_str(), "r");
    if (!pemFile)
        return false;
    EVP_PKEYPtr pkey =
        makeEVPPKeyPtr(PEM_read_PrivateKey(pemFile, nullptr, nullptr, nullptr));
    fclose(pemFile);
    if (!pkey)
        return false;

    FILE* derFile = fopen(derPath.c_str(), "wb");
    if (!derFile)
    {
        return false;
    }
    int ret = i2d_PrivateKey_fp(derFile, pkey.get());
    fclose(derFile);

    return ret == 1;
}

// Convert DER private key file to PEM format and save to file
bool derKeyToPem(const std::string& derPath, const std::string& pemPath)
{
    FILE* derFile = fopen(derPath.c_str(), "rb");
    if (!derFile)
        return false;
    EVP_PKEYPtr pkey = makeEVPPKeyPtr(d2i_PrivateKey_fp(derFile, nullptr));
    fclose(derFile);
    if (!pkey)
        return false;

    FILE* pemFile = fopen(pemPath.c_str(), "w");
    if (!pemFile)
    {
        return false;
    }
    int ret = PEM_write_PrivateKey(pemFile, pkey.get(), nullptr, nullptr, 0,
                                   nullptr, nullptr);
    fclose(pemFile);
    return ret == 1;
}
bool loadTpm2(boost::asio::ssl::context& ctx)
{
    // Attempt to load certificate and private key from TPM2 using OpenSSL
    // engine This assumes the TPM2 OpenSSL engine is installed and configured
    // Example key URI: "tpm2tss-engine:0x81010001"
    const char* tpm2_engine_id = "tpm2tss";
    const char* tpm2_key_id =
        "0x81010001"; // Change as appropriate for your TPM

    ENGINE_load_dynamic();
    ENGINE* e = ENGINE_by_id(tpm2_engine_id);
    if (!e)
    {
        LOG_ERROR("Failed to load TPM2 engine '{}'", tpm2_engine_id);
        return false;
    }
    if (!ENGINE_init(e))
    {
        LOG_ERROR("Failed to initialize TPM2 engine");
        ENGINE_free(e);
        return false;
    }

    // Load private key from TPM2
    EVP_PKEY* pkey = ENGINE_load_private_key(e, tpm2_key_id, nullptr, nullptr);
    if (!pkey)
    {
        LOG_ERROR("Failed to load private key from TPM2");
        ENGINE_finish(e);
        ENGINE_free(e);
        return false;
    }

    // Load certificate from file or TPM2 (usually certificate is not in TPM2,
    // so load from file)
    FILE* certfile = fopen(ENTITY_CERT_PATH, "r");
    if (!certfile)
    {
        LOG_ERROR("Failed to open entity certificate file: {}",
                  ENTITY_CERT_PATH);
        EVP_PKEY_free(pkey);
        ENGINE_finish(e);
        ENGINE_free(e);
        return false;
    }
    X509* cert = PEM_read_X509(certfile, nullptr, nullptr, nullptr);
    fclose(certfile);
    if (!cert)
    {
        LOG_ERROR("Failed to read entity certificate from file");
        EVP_PKEY_free(pkey);
        ENGINE_finish(e);
        ENGINE_free(e);
        return false;
    }

    // Set certificate and private key in SSL context
    if (SSL_CTX_use_certificate(ctx.native_handle(), cert) != 1)
    {
        LOG_ERROR("Failed to set certificate in SSL context");
        X509_free(cert);
        EVP_PKEY_free(pkey);
        ENGINE_finish(e);
        ENGINE_free(e);
        return false;
    }
    if (SSL_CTX_use_PrivateKey(ctx.native_handle(), pkey) != 1)
    {
        LOG_ERROR("Failed to set private key in SSL context");
        X509_free(cert);
        EVP_PKEY_free(pkey);
        ENGINE_finish(e);
        ENGINE_free(e);
        return false;
    }

    X509_free(cert);
    EVP_PKEY_free(pkey);
    ENGINE_finish(e);
    ENGINE_free(e);

    LOG_DEBUG("Loaded certificate and private key from TPM2");
    return true;
}
openssl_ptr<EVP_PKEY, EVP_PKEY_free> loadPrivateKey(const std::string& path)
{
    BIO* keybio = BIO_new_file(path.data(), "r");
    if (!keybio)
        return {nullptr, EVP_PKEY_free};
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(keybio, nullptr, nullptr, nullptr);
    BIO_free(keybio);
    return openssl_ptr<EVP_PKEY, EVP_PKEY_free>(pkey, EVP_PKEY_free);
}
openssl_ptr<X509_NAME, X509_NAME_free> generateCAName(
    const std::string& common_name)
{
    openssl_ptr<X509_NAME, X509_NAME_free> name(X509_NAME_new(),
                                                X509_NAME_free);
    if (!name)
        return {nullptr, X509_NAME_free};
    if (X509_NAME_add_entry_by_txt(name.get(), "CN", MBSTRING_ASC,
                                   (const unsigned char*)common_name.c_str(),
                                   -1, -1, 0) != 1)
    {
        return {nullptr, X509_NAME_free};
    }
    return name;
}
// Generate a new EVP_PKEY (RSA 2048)
inline openssl_ptr<EVP_PKEY, EVP_PKEY_free> generate_key()
{
    openssl_ptr<EVP_PKEY, EVP_PKEY_free> pkey(EVP_PKEY_new(), EVP_PKEY_free);
    openssl_ptr<RSA, RSA_free> rsa(RSA_new(), RSA_free);
    BIGNUM* e = BN_new();
    BN_set_word(e, RSA_F4);
    RSA_generate_key_ex(rsa.get(), 2048, e, nullptr);
    EVP_PKEY_assign_RSA(pkey.get(), rsa.release());
    BN_free(e);
    return pkey;
}

// Create a new X509 certificate, signed by issuer_pkey, with subject/issuer
// names
inline openssl_ptr<X509, X509_free> create_certificate(
    EVP_PKEY* subject_pkey, X509_NAME* subject_name, EVP_PKEY* issuer_pkey,
    X509_NAME* issuer_name, int days_valid, bool is_ca)
{
    openssl_ptr<X509, X509_free> cert(X509_new(), X509_free);
    ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), std::rand());
    X509_gmtime_adj(X509_get_notBefore(cert.get()), 0);
    X509_gmtime_adj(X509_get_notAfter(cert.get()),
                    60L * 60L * 24L * days_valid);
    X509_set_pubkey(cert.get(), subject_pkey);
    X509_set_subject_name(cert.get(), subject_name);
    X509_set_issuer_name(cert.get(), issuer_name);

    // Add basicConstraints
    X509_EXTENSION* ext;
    ext = X509V3_EXT_conf_nid(nullptr, nullptr, NID_basic_constraints,
                              is_ca ? (char*)"CA:TRUE" : (char*)"CA:FALSE");
    X509_add_ext(cert.get(), ext, -1);
    X509_EXTENSION_free(ext);

    // Add keyUsage
    if (is_ca)
    {
        ext = X509V3_EXT_conf_nid(nullptr, nullptr, NID_key_usage,
                                  (char*)"keyCertSign, cRLSign");
    }
    else
    {
        ext = X509V3_EXT_conf_nid(nullptr, nullptr, NID_key_usage,
                                  (char*)"digitalSignature, keyEncipherment");
    }
    X509_add_ext(cert.get(), ext, -1);
    X509_EXTENSION_free(ext);

    X509_sign(cert.get(), issuer_pkey, EVP_sha256());
    return cert;
}

// Create an intermediate CA certificate signed by root CA
inline bool create_intermediate_ca_cert(
    EVP_PKEY* root_pkey, X509_NAME* root_name,
    openssl_ptr<EVP_PKEY, EVP_PKEY_free>& inter_pkey,
    openssl_ptr<X509, X509_free>& inter_cert, const std::string& common_name,
    int days_valid = 365)
{
    inter_pkey = generate_key();
    openssl_ptr<X509_NAME, X509_NAME_free> inter_name(X509_NAME_new(),
                                                      X509_NAME_free);
    X509_NAME_add_entry_by_txt(inter_name.get(), "CN", MBSTRING_ASC,
                               (const unsigned char*)common_name.c_str(), -1,
                               -1, 0);

    inter_cert = create_certificate(
        inter_pkey.get(), inter_name.get(), root_pkey, root_name, days_valid,
        true // is_ca
    );
    return inter_cert != nullptr;
}

// Create an entity certificate signed by CA (root or intermediate)
inline bool create_entity_cert(
    EVP_PKEY* ca_pkey, X509_NAME* ca_name,
    openssl_ptr<EVP_PKEY, EVP_PKEY_free>& entity_pkey,
    openssl_ptr<X509, X509_free>& entity_cert, const std::string& common_name,
    int days_valid = 365)
{
    entity_pkey = generate_key();
    openssl_ptr<X509_NAME, X509_NAME_free> entity_name(X509_NAME_new(),
                                                       X509_NAME_free);
    X509_NAME_add_entry_by_txt(entity_name.get(), "CN", MBSTRING_ASC,
                               (const unsigned char*)common_name.c_str(), -1,
                               -1, 0);

    entity_cert = create_certificate(
        entity_pkey.get(), entity_name.get(), ca_pkey, ca_name, days_valid,
        false // is_ca
    );
    return entity_cert != nullptr;
}
bool saveCertificateToFile(const std::string& path,
                           const openssl_ptr<X509, X509_free>& cert)
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        LOG_ERROR("Failed to open file {} for writing", path);
        return false;
    }
    openssl_ptr<BIO, BIO_free_all> bio(BIO_new(BIO_s_mem()), BIO_free_all);
    if (!PEM_write_bio_X509(bio.get(), cert.get()))
    {
        LOG_ERROR("Failed to write certificate to BIO");
        return false;
    }
    BUF_MEM* buf;
    BIO_get_mem_ptr(bio.get(), &buf);
    file.write(buf->data, buf->length);
    if (!file)
    {
        LOG_ERROR("Failed to write certificate to file {}", path);
        return false;
    }
    file.close();
    LOG_DEBUG("Certificate saved to {}", path);
    return true;
}
bool savePrivateKeyToFile(const std::string& path,
                          const openssl_ptr<EVP_PKEY, EVP_PKEY_free>& pkey)
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        LOG_ERROR("Failed to open file {} for writing", path);
        return false;
    }
    openssl_ptr<BIO, BIO_free_all> bio(BIO_new(BIO_s_mem()), BIO_free_all);
    if (!PEM_write_bio_PrivateKey(bio.get(), pkey.get(), nullptr, nullptr, 0,
                                  nullptr, nullptr))
    {
        LOG_ERROR("Failed to write private key to BIO");
        return false;
    }
    BUF_MEM* buf;
    BIO_get_mem_ptr(bio.get(), &buf);
    file.write(buf->data, buf->length);
    if (!file)
    {
        LOG_ERROR("Failed to write private key to file {}", path);
        return false;
    }
    file.close();
    LOG_DEBUG("Private key saved to {}", path);
    return true;
}
std::string toString(const openssl_ptr<X509, X509_free>& cert)
{
    openssl_ptr<BIO, BIO_free_all> bio(BIO_new(BIO_s_mem()), BIO_free_all);
    if (!PEM_write_bio_X509(bio.get(), cert.get()))
    {
        LOG_ERROR("Failed to write certificate to BIO");
        return {};
    }
    BUF_MEM* buf;
    BIO_get_mem_ptr(bio.get(), &buf);
    return std::string(buf->data, buf->length);
}
std::string toString(const openssl_ptr<EVP_PKEY, EVP_PKEY_free>& pkey)
{
    openssl_ptr<BIO, BIO_free_all> bio(BIO_new(BIO_s_mem()), BIO_free_all);
    if (!PEM_write_bio_PrivateKey(bio.get(), pkey.get(), nullptr, nullptr, 0,
                                  nullptr, nullptr))
    {
        LOG_ERROR("Failed to write private key to BIO");
        return {};
    }
    BUF_MEM* buf;
    BIO_get_mem_ptr(bio.get(), &buf);
    return std::string(buf->data, buf->length);
}
bool createAndSaveEntityCertificate(EVP_PKEY* ca_pkey, X509_NAME* ca_name,
                                    const std::string& common_name)
{
    openssl_ptr<EVP_PKEY, EVP_PKEY_free> entitykey(nullptr, EVP_PKEY_free);
    openssl_ptr<X509, X509_free> entitiycert(nullptr, X509_free);

    if (!create_entity_cert(ca_pkey, ca_name, entitykey, entitiycert,
                            common_name))
    {
        LOG_ERROR("Failed to create entity certificate");
        return false;
    }
    if (!savePrivateKeyToFile(PKEY_PATH, entitykey))
    {
        LOG_ERROR("Failed to save private key to {}", PKEY_PATH);
        return false;
    }
    if (!saveCertificateToFile(ENTITY_CERT_PATH, entitiycert))
    {
        LOG_ERROR("Failed to save entity certificate to {}", ENTITY_CERT_PATH);
        return false;
    }
    LOG_DEBUG("Entity certificate and private key saved to {} and {}",
              ENTITY_CERT_PATH, PKEY_PATH);
    return true;
}
struct CertificateExchanger
{
    EVP_PKEY* ca_pkey{nullptr};
    X509_NAME* ca_name{nullptr};
    EventQueue& eventQueue;
    net::io_context& ioContext;
    CertificateExchanger(EVP_PKEY* ca_pkey, X509_NAME* ca_name,
                         EventQueue& eventQueue, net::io_context& ioContext) :
        ca_pkey(ca_pkey), ca_name(ca_name), eventQueue(eventQueue),
        ioContext(ioContext)
    {}
    CertificateExchanger(const CertificateExchanger&) = delete;
    CertificateExchanger& operator=(const CertificateExchanger&) = delete;

    net::awaitable<bool> exchange(Streamer streamer)
    {
        LOG_DEBUG("Exchanging certificates");
        if (!co_await sendCertificate(streamer))
        {
            LOG_ERROR("Failed to send certificates");
            co_return false;
        }
        if (!co_await recieveCertificate(streamer))
        {
            LOG_ERROR("Failed to receive certificate");
            co_return false;
        }
        LOG_DEBUG("Certificate exchange completed successfully");
        co_return true;
    }
    net::awaitable<bool> waitForExchange(Streamer streamer)
    {
        if (!co_await recieveCertificate(streamer))
        {
            LOG_ERROR("Failed to receive certificate");
            co_return false;
        }
        if (!co_await sendCertificate(streamer))
        {
            LOG_ERROR("Failed to send certificate");
            co_return false;
        }
        co_return true;
    }

    bool processInterMediateCA(const openssl_ptr<EVP_PKEY, EVP_PKEY_free>& pkey,
                               const openssl_ptr<X509, X509_free>& ca)
    {
        if (!pkey)
        {
            LOG_ERROR("Failed to read private key from provided data");
            return false;
        }
        if (!ca)
        {
            LOG_ERROR("Failed to read CA certificate from provided data");
            return false;
        }
        auto caname = openssl_ptr<X509_NAME, X509_NAME_free>(
            X509_NAME_dup(X509_get_subject_name(ca.get())), X509_NAME_free);
        createCertDirectories();
        if (!createAndSaveEntityCertificate(pkey.get(), caname.get(),
                                            "BMC Entity"))
        {
            LOG_ERROR("Failed to create entity certificate");
            return false;
        }

        return true;
    }
    bool installCertificates(const std::string& castr)
    {
        openssl_ptr<X509, X509_free> ca(
            PEM_read_bio_X509(BIO_new_mem_buf(castr.data(), castr.size()),
                              nullptr, nullptr, nullptr),
            X509_free);

        if (!saveCertificateToFile(CA_PATH, ca))
        {
            LOG_ERROR("Failed to save CA certificate to {}", CA_PATH);
            return false;
        }
        LOG_DEBUG("Certificates written to {} , {} and {}", CA_PATH, PKEY_PATH,
                  ENTITY_CERT_PATH);
        return true;
    }
    net::awaitable<bool> sendCertificate(Streamer streamer)
    {
        openssl_ptr<EVP_PKEY, EVP_PKEY_free> pkey(nullptr, EVP_PKEY_free);
        openssl_ptr<X509, X509_free> ca(nullptr, X509_free);
        if (!create_intermediate_ca_cert(ca_pkey, ca_name, pkey, ca, "BMC CA"))
        {
            LOG_ERROR("Failed to create intermediate CA certificate");
            co_return false;
        }
        if (!processInterMediateCA(pkey, ca))
        {
            LOG_ERROR("Failed to process intermediate CA");
            co_return false;
        }

        std::string intermediate_ca =
            toString(ca); // Convert to string for sending

        nlohmann::json jsonBody;
        jsonBody["CA"] = intermediate_ca;
        auto [ec, size] = co_await sendHeader(
            streamer, makeEvent(INSTALL_CERTIFICATES, jsonBody.dump()));
        if (ec)
        {
            LOG_ERROR("Failed to send INSTALL_CERTIFICATES event: {}",
                      ec.message());
            co_return false;
        }
        if (!co_await recieveCertificateStatus(streamer))
        {
            LOG_ERROR("Failed to Install certificates");
            co_return false;
        }
        LOG_DEBUG("Certificates installed successfully");
        co_return true;
    }
    net::awaitable<bool> recieveCertificateStatus(Streamer streamer)
    {
        auto [ec, event] = co_await readHeader(streamer);
        if (ec)
        {
            LOG_ERROR("Failed to read response: {}", ec.message());
            co_return false;
        }
        auto [id, body] = parseEvent(event);
        if (id == INSTALL_CERTIFICATES_RESP)
        {
            auto jsonBody = nlohmann::json::parse(body);
            auto installed = jsonBody["status"].get<bool>();
            if (!installed)
            {
                LOG_ERROR("Failed to install certificates");
                co_return false;
            }
            LOG_DEBUG("Certificates installed successfully");
            co_return true;
        }

        LOG_ERROR("Unexpected event ID: {}", id);
        co_return false;
    }
    net::awaitable<bool> sendInstallStatus(Streamer& streamer, bool status)
    {
        nlohmann::json jsonBody;
        jsonBody["status"] = status;
        auto [ec, size] = co_await sendHeader(
            streamer, makeEvent(INSTALL_CERTIFICATES_RESP, jsonBody.dump()));
        if (ec)
        {
            LOG_ERROR("Failed to send INSTALL_CERTIFICATES_RESP event: {}",
                      ec.message());
            co_return false;
        }
        co_return status;
    }
    net::awaitable<bool> recieveCertificate(Streamer streamer)
    {
        auto [ec, event] = co_await readHeader(streamer);
        if (ec)
        {
            LOG_ERROR("Failed to read response: {}", ec.message());
            co_return false;
        }
        auto [id, body] = parseEvent(event);
        if (id == INSTALL_CERTIFICATES)
        {
            auto jsonBody = nlohmann::json::parse(body);
            auto CA = jsonBody["CA"].get<std::string>();
            if (CA.empty())
            {
                LOG_ERROR("CA or PKEY is empty in the event body");
                co_return co_await sendInstallStatus(streamer, false);
            }
            if (!installCertificates(CA))
            {
                LOG_ERROR("Failed to install certificates");
                co_return co_await sendInstallStatus(streamer, false);
            }
            co_return co_await sendInstallStatus(streamer, true);
        }
        LOG_ERROR("Unexpected event ID: {}", id);
        co_return false;
    }
};
