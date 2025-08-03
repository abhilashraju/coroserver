#include "cert_generator.hpp"
#include "command_line_parser.hpp"
#include "measurements.hpp"

#include <format>
#include <optional>
constexpr auto CLIENT_PKEY_NAME = "client_key.{}";
constexpr auto ENTITY_CLIENT_CERT_NAME = "client_cert.{}";
constexpr auto SERVER_PKEY_NAME = "server_key.{}";
constexpr auto ENTITY_SERVER_CERT_NAME = "server_cert.{}";
std::optional<std::pair<X509Ptr, EVP_PKEYPtr>> createAndSaveEntityCertificate(
    const EVP_PKEYPtr& ca_pkey, const X509Ptr& ca,
    const std::string& common_name, bool server, const std::string& format)
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
    using ENTITY_DATA = std::tuple<const char*, std::string, std::string>;
    std::array<ENTITY_DATA, 2> entity_data = {
        ENTITY_DATA{"clientAuth", std::format(CLIENT_PKEY_NAME, format),
                    std::format(ENTITY_CLIENT_CERT_NAME, format)},
        ENTITY_DATA{"serverAuth", std::format(SERVER_PKEY_NAME, format),
                    std::format(ENTITY_SERVER_CERT_NAME, format)}};

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
    if (!savePrivateKey(std::get<1>(entity_data[server]), key, format == "pem"))
    {
        LOG_ERROR("Failed to save private key to {}",
                  std::get<1>(entity_data[server]));
        return std::nullopt;
    }
    std::vector<X509*> cert_chain;
    cert_chain.emplace_back(cert.get());
    cert_chain.emplace_back(ca.get());
    std::string filename = std::get<2>(entity_data[server]);
    if (!saveCertificate(filename, cert_chain, format == "pem"))
    {
        LOG_ERROR("Failed to save entity certificate to {}",
                  std::get<2>(entity_data[server]));
        return std::nullopt;
    }
    LOG_DEBUG("Entity certificate and private key saved to {} and {}",
              std::get<2>(entity_data[server]),
              std::get<1>(entity_data[server]));
    return std::make_optional(std::make_pair(std::move(cert), std::move(key)));
}
bool processInterMediateCA(const openssl_ptr<EVP_PKEY, EVP_PKEY_free>& pkey,
                           const openssl_ptr<X509, X509_free>& ca,
                           const std::string& formatStr)
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
        createAndSaveEntityCertificate(pkey, ca, "BMC Entity", true, formatStr);
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
    auto clientCertsdata = createAndSaveEntityCertificate(
        pkey, ca, "BMC Entity", false, formatStr);
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
int main(int argc, const char* argv[])
{
    auto [format] = getArgs(parseCommandline(argc, argv), "-format,-f");
    std::string formatStr = "pem";
    if (format.has_value())
    {
        formatStr = format.value();
    }
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

    if (!processInterMediateCA(pkey, ca, formatStr))
    {
        LOG_ERROR("Failed to process intermediate CA");
        return -1;
    }
    saveCertificate(std::format("ca.{}", formatStr), ca, formatStr == "pem");
    savePrivateKey(std::format("ca_key.{}", formatStr), pkey,
                   formatStr == "pem");
    MeasurementTaker taker(loadPrivateKey(
        std::format(CLIENT_PKEY_NAME, formatStr), formatStr == "pem"));
    MeasurementVerifier verifier(getPublicKeyFromCert(loadCertificate(
        std::format(ENTITY_CLIENT_CERT_NAME, formatStr), formatStr == "pem")));
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
