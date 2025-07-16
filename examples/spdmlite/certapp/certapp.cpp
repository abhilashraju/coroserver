#include "cert_generator.hpp"
#include "measurements.hpp"

#include <optional>
constexpr auto CLIENT_PKEY_NAME = "client_key.pem";
constexpr auto ENTITY_CLIENT_CERT_NAME = "client_cert.pem";
constexpr auto SERVER_PKEY_NAME = "server_key.pem";
constexpr auto ENTITY_SERVER_CERT_NAME = "server_cert.pem";
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
        ENTITY_DATA{"clientAuth", CLIENT_PKEY_NAME, ENTITY_CLIENT_CERT_NAME},
        ENTITY_DATA{"serverAuth", SERVER_PKEY_NAME, ENTITY_SERVER_CERT_NAME}};

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
    auto certsdata =
        createAndSaveEntityCertificate(pkey, ca, "BMC Entity", true);
    if (!certsdata)
    {
        LOG_ERROR("Failed to create server entity certificate");
        return false;
    }
    auto [serverCert, serverKey] = std::move(*certsdata);

    // auto serverCert = loadCertificate(ENTITY_SERVER_CERT_NAME);
    if (!isSignedByCA(serverCert, getPublicKeyFromCert(ca)))
    {
        LOG_ERROR("Failed to verify signature of server certificate");
    }
    auto clientCertsdata =
        createAndSaveEntityCertificate(pkey, ca, "BMC Entity", false);
    if (!clientCertsdata)
    {
        LOG_ERROR("Failed to create client entity certificate");
        return false;
    }
    auto [clientCert, clientKey] = std::move(*clientCertsdata);

    // auto clientCert = loadCertificate(ENTITY_CLIENT_CERT_NAME);
    if (!isSignedByCA(clientCert, getPublicKeyFromCert(ca)))
    {
        LOG_ERROR("Failed to verify signature of  client certificate");
    }

    return true;
}
int main()
{
    // EVP_set_default_properties(NULL, "provider=default,provider=legacy");
    auto [ca, pkey] = create_ca_cert(nullptr, nullptr, "BMC CA");
    if (!ca || !pkey)
    {
        LOG_ERROR("Failed to create CA certificate and private key");
        return -1;
    }

    if (!isSignedByCA(ca, getPublicKeyFromCert(ca)))
    {
        LOG_ERROR("Failed to verify signature of CA certificate");
    }

    if (!processInterMediateCA(pkey, ca))
    {
        LOG_ERROR("Failed to process intermediate CA");
        return -1;
    }
    saveCertificateToFile("ca.pem", ca);
    savePrivateKeyToFile("ca_key.pem", pkey);
    MeasurementTaker taker(loadPrivateKey(CLIENT_PKEY_NAME));
    MeasurementVerifier verifier(
        getPublicKeyFromCert(loadCertificate(ENTITY_CLIENT_CERT_NAME)));
    auto measurement = taker("/usr/bin/spdmlite");
    auto isValid = verifier("/usr/bin/spdmlite", measurement);
    if (isValid)
    {
        LOG_DEBUG("Measurement verification succeeded");
    }
    else
    {
        LOG_ERROR("Measurement verification failed");
    }
    return 0;
}
