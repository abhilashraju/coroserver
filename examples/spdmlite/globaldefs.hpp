#pragma once
#include "logger.hpp"

#include <filesystem>
constexpr auto CA_PATH = "/tmp/etc/ssl/certs/ca.pem";
constexpr auto PKEY_PATH = "/tmp/etc/ssl/private/pkey.pem";
constexpr auto ENTITY_CERT_PATH = "/tmp/etc/ssl/certs/https/entity_cert.pem";

inline void createCertDirectories()
{
    if (std::filesystem::exists(std::filesystem::path(CA_PATH).parent_path()) &&
        std::filesystem::exists(
            std::filesystem::path(PKEY_PATH).parent_path()) &&
        std::filesystem::exists(
            std::filesystem::path(ENTITY_CERT_PATH).parent_path()))
    {
        LOG_DEBUG("Directories already exist, skipping creation");
        return;
    }
    std::filesystem::create_directories(
        std::filesystem::path(CA_PATH).parent_path());
    std::filesystem::create_directories(
        std::filesystem::path(PKEY_PATH).parent_path());
    std::filesystem::create_directories(
        std::filesystem::path(ENTITY_CERT_PATH).parent_path());
}
inline void clearCertificates()
{
    if (std::filesystem::exists(std::filesystem::path(CA_PATH)))
    {
        std::filesystem::remove(std::filesystem::path(CA_PATH));
    }
    if (std::filesystem::exists(std::filesystem::path(PKEY_PATH)))
    {
        std::filesystem::remove(std::filesystem::path(PKEY_PATH));
    }
    if (std::filesystem::exists(std::filesystem::path(ENTITY_CERT_PATH)))
    {
        std::filesystem::remove(std::filesystem::path(ENTITY_CERT_PATH));
    }
}
