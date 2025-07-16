#pragma once
#include "cert_generator.hpp"
#include "eventmethods.hpp"
#include "eventqueue.hpp"
#include "globaldefs.hpp"
constexpr auto INSTALL_CERTIFICATES = "InstallCertificates";
constexpr auto INSTALL_CERTIFICATES_RESP = "InstallCertificatesResp";
std::optional<std::pair<X509Ptr, EVP_PKEYPtr>> createAndSaveEntityCertificate(
    const EVP_PKEYPtr& ca_pkey, const X509Ptr& ca,
    const std::string& common_name, bool server)
{
    auto ca_name = openssl_ptr<X509_NAME, X509_NAME_free>(
        X509_NAME_dup(X509_get_subject_name(ca.get())), X509_NAME_free);
    auto [cert,
          key] = create_leaf_cert(ca_pkey.get(), ca_name.get(), common_name);
    if (!cert || !key)
    {
        LOG_ERROR("Failed to create entity certificate");
        return std::nullopt;
    }
    using ENTITY_DATA = std::tuple<const char*, const char*, const char*>;
    std::array<ENTITY_DATA, 2> entity_data = {
        ENTITY_DATA{"clientAuth", CLIENT_PKEY_PATH, ENTITY_CLIENT_CERT_PATH},
        ENTITY_DATA{"serverAuth", SERVER_PKEY_PATH, ENTITY_SERVER_CERT_PATH}};

    // Add serverAuth extended key usage
    // openssl_ptr<X509_EXTENSION, X509_EXTENSION_free> ext(
    //     X509V3_EXT_conf_nid(nullptr, nullptr, NID_ext_key_usage,
    //                         (char*)std::get<0>(entity_data[server])),
    //     X509_EXTENSION_free);
    // if (!ext)
    // {
    //     LOG_ERROR("Failed to add serverAuth extension");
    //     return std::nullopt;
    // }
    // X509_add_ext(cert.get(), ext.get(), -1);
    if (!savePrivateKeyToFile(std::get<1>(entity_data[server]), key))
    {
        LOG_ERROR("Failed to save private key to {}",
                  std::get<1>(entity_data[server]));
        return std::nullopt;
    }
    std::vector<X509*> cert_chain;
    cert_chain.emplace_back(cert.get());
    cert_chain.emplace_back(ca.get());

    // Save the combined certificate chain to a new file
    FILE* output_file = fopen(std::get<2>(entity_data[server]), "w");
    for (auto cert : cert_chain)
    {
        PEM_write_X509(output_file, cert);
    }
    fclose(output_file);
    LOG_DEBUG("Entity certificate and private key saved to {} and {}",
              std::get<2>(entity_data[server]),
              std::get<1>(entity_data[server]));
    return std::make_optional(std::make_pair(std::move(cert), std::move(key)));
}
struct CertificateExchanger
{
    EventQueue& eventQueue;
    net::io_context& ioContext;
    CertificateExchanger(EventQueue& eventQueue, net::io_context& ioContext) :
        eventQueue(eventQueue), ioContext(ioContext)
    {}
    CertificateExchanger(const CertificateExchanger&) = delete;
    CertificateExchanger& operator=(const CertificateExchanger&) = delete;

    net::awaitable<bool> exchange(Streamer streamer)
    {
        createCertDirectories();
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
        createCertDirectories();
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
        auto servCert =
            createAndSaveEntityCertificate(pkey, ca, "BMC Entity", true);
        if (!servCert)
        {
            LOG_ERROR("Failed to create server entity certificate");
            return false;
        }
        auto clientCert =
            createAndSaveEntityCertificate(pkey, ca, "BMC Entity", false);
        if (!clientCert)
        {
            LOG_ERROR("Failed to create client entity certificate");
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
        LOG_DEBUG("CA Certificates written to {} ", CA_PATH);
        return true;
    }
    net::awaitable<bool> sendCertificate(Streamer streamer)
    {
        auto [ca_cert, ca_pkey] = create_ca_cert(nullptr, nullptr, "BMC CA");
        if (!ca_cert || !ca_pkey)
        {
            LOG_ERROR("Failed to create CA certificate and private key");
            co_return false;
        }
        if (!processInterMediateCA(ca_pkey, ca_cert))
        {
            LOG_ERROR("Failed to process intermediate CA");
            co_return false;
        }

        std::string intermediate_ca =
            toString(ca_cert); // Convert to string for sending

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
