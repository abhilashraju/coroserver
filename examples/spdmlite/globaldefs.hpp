#pragma once
#include "logger.hpp"

#include <filesystem>
constexpr auto CA_PATH = "/tmp/etc/ssl/certs/ca.pem";
constexpr auto SERVER_PKEY_PATH = "/tmp/etc/ssl/private/server_pkey.pem";
constexpr auto ENTITY_SERVER_CERT_PATH =
    "/tmp/etc/ssl/certs/https/server_cert.pem";
constexpr auto CLIENT_PKEY_PATH = "/tmp/etc/ssl/private/client_pkey.pem";
constexpr auto ENTITY_CLIENT_CERT_PATH =
    "/tmp/etc/ssl/certs/https/client_cert.pem";

inline void createCertDirectories()
{
    if (std::filesystem::exists(std::filesystem::path(CA_PATH).parent_path()) &&
        std::filesystem::exists(
            std::filesystem::path(SERVER_PKEY_PATH).parent_path()) &&
        std::filesystem::exists(
            std::filesystem::path(ENTITY_SERVER_CERT_PATH).parent_path()))
    {
        LOG_DEBUG("Directories already exist, skipping creation");
        return;
    }
    LOG_DEBUG("Creating directories for certificates at {}, {}, {}", CA_PATH,
              SERVER_PKEY_PATH, ENTITY_SERVER_CERT_PATH);
    std::filesystem::create_directories(
        std::filesystem::path(CA_PATH).parent_path());
    std::filesystem::create_directories(
        std::filesystem::path(SERVER_PKEY_PATH).parent_path());
    std::filesystem::create_directories(
        std::filesystem::path(ENTITY_SERVER_CERT_PATH).parent_path());
}
inline void clearCertificates()
{
    if (std::filesystem::exists(std::filesystem::path(CA_PATH)))
    {
        std::filesystem::remove(std::filesystem::path(CA_PATH));
    }
    if (std::filesystem::exists(std::filesystem::path(SERVER_PKEY_PATH)))
    {
        std::filesystem::remove(std::filesystem::path(SERVER_PKEY_PATH));
    }
    if (std::filesystem::exists(std::filesystem::path(ENTITY_SERVER_CERT_PATH)))
    {
        std::filesystem::remove(std::filesystem::path(ENTITY_SERVER_CERT_PATH));
    }
    if (std::filesystem::exists(std::filesystem::path(CLIENT_PKEY_PATH)))
    {
        std::filesystem::remove(std::filesystem::path(CLIENT_PKEY_PATH));
    }
    if (std::filesystem::exists(std::filesystem::path(ENTITY_CLIENT_CERT_PATH)))
    {
        std::filesystem::remove(std::filesystem::path(ENTITY_CLIENT_CERT_PATH));
    }
}
