#pragma once
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>

std::string getExecutableMeasurement(const std::string& exePath,
                                     const std::string& privKeyPath)
{
    // Open the executable file
    std::ifstream file(exePath, std::ios::binary);
    if (!file)
        return {};

    // Read the entire file into a buffer
    std::vector<unsigned char> fileData((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());

    // Load private key
    EVP_PKEY* pkey = nullptr;
    {
        BIO* keybio = BIO_new_file(privKeyPath.data(), "r");
        if (!keybio)
            return {};
        pkey = PEM_read_bio_PrivateKey(keybio, nullptr, nullptr, nullptr);
        BIO_free(keybio);
        if (!pkey)
            return {};
    }

    // Sign the file using EVP_DigestSign* APIs
    std::string signature;
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx)
    {
        EVP_PKEY_free(pkey);
        return {};
    }

    if (EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey) == 1)
    {
        if (EVP_DigestSignUpdate(mdctx, fileData.data(), fileData.size()) == 1)
        {
            size_t sigLen = 0;
            if (EVP_DigestSignFinal(mdctx, nullptr, &sigLen) == 1)
            {
                std::vector<unsigned char> sig(sigLen);
                if (EVP_DigestSignFinal(mdctx, sig.data(), &sigLen) == 1)
                {
                    std::ostringstream oss;
                    for (size_t i = 0; i < sigLen; ++i)
                    {
                        oss << std::hex << std::setw(2) << std::setfill('0')
                            << static_cast<int>(sig[i]);
                    }
                    signature = oss.str();
                }
            }
        }
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);

    return signature;
}
bool verifyExecutableMeasurement(const std::string& exePath,
                                 const std::string& pubKeyPath,
                                 const std::string& signatureHex)
{
    // Open the executable file
    std::ifstream file(exePath, std::ios::binary);
    if (!file)
        return false;

    // Read the entire file into a buffer
    std::vector<unsigned char> fileData((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());

    // Convert hex signature to binary
    if (signatureHex.length() % 2 != 0)
        return false;
    std::vector<unsigned char> signature(signatureHex.length() / 2);
    for (size_t i = 0; i < signature.size(); ++i)
    {
        unsigned int byte;
        std::istringstream iss(signatureHex.substr(2 * i, 2));
        iss >> std::hex >> byte;
        signature[i] = static_cast<unsigned char>(byte);
    }

    // Load public key
    EVP_PKEY* pkey = nullptr;
    {
        BIO* keybio = BIO_new_file(pubKeyPath.data(), "r");
        if (!keybio)
            return false;
        pkey = PEM_read_bio_PUBKEY(keybio, nullptr, nullptr, nullptr);
        BIO_free(keybio);
        if (!pkey)
            return false;
    }

    // Use EVP_DigestVerify* APIs to verify the signature
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx)
    {
        EVP_PKEY_free(pkey);
        return false;
    }

    bool result = false;
    if (EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey) == 1)
    {
        if (EVP_DigestVerifyUpdate(mdctx, fileData.data(), fileData.size()) ==
            1)
        {
            if (EVP_DigestVerifyFinal(mdctx, signature.data(),
                                      signature.size()) == 1)
            {
                result = true;
            }
        }
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);

    return result;
}
struct MeasurementTaker
{
    std::string privkey;
    MeasurementTaker(const std::string& privkeyPath) : privkey(privkeyPath) {}
    std::string operator()(const std::string& exePath)
    {
        return getExecutableMeasurement(exePath, privkey);
    }
};
struct MeasurementVerifier
{
    std::string pubkey;
    MeasurementVerifier(const std::string& pubKey) : pubkey(pubKey) {}
    bool operator()(const std::string& exePath, const std::string& measurement)
    {
        return verifyExecutableMeasurement(exePath, pubkey, measurement);
    }
};
