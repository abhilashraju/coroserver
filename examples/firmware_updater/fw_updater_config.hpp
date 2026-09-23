#pragma once
#include "logger.hpp"

#include <nlohmann/json.hpp>

#include <expected>
#include <fstream>
#include <string>
#include <vector>

namespace firmware_updater
{

/**
 * @brief Describes a firmware image served by the remote update server.
 *
 * The remote server is expected to expose a JSON catalogue at a well-known
 * path (configured via cataloguePath).  Each entry in the catalogue array
 * maps to one FirmwareEntry.
 */
struct FirmwareEntry
{
    std::string version;     ///< Semantic version string, e.g. "2.15.0"
    std::string imageUrl;    ///< Absolute URL to the firmware binary
    std::string checksum;    ///< SHA-256 hex digest of the binary (optional)
    std::string releaseDate; ///< ISO-8601 date, e.g. "2024-12-01"
    std::string description; ///< Human-readable release summary
};

/**
 * @brief Authentication credentials for the remote update server.
 */
struct ServerAuth
{
    std::string username;
    std::string password;
};

/**
 * @brief Top-level application configuration loaded from JSON.
 *
 * Example file: config/firmware_updater.json
 */
struct AppConfig
{
    // ── Remote update server ──────────────────────────────────────────────
    std::string serverHost;          ///< Hostname or IP of the update server
    std::string serverPort{"443"};   ///< HTTPS port
    std::string cataloguePath{
        "/firmware/catalogue.json"}; ///< Path on server returning the
                                     ///< FW catalogue JSON array
    ServerAuth auth;

    // ── Polling ───────────────────────────────────────────────────────────
    int pollIntervalSeconds{3600}; ///< 0 → poll only at startup
    bool pollAtStartup{true};      ///< Run one check immediately on start

    // ── Version policy ────────────────────────────────────────────────────
    /// Versions listed here will NOT be downloaded even if newer.
    std::vector<std::string> deniedVersions;
    /// When non-empty, only versions in this list are accepted.
    std::vector<std::string> allowedVersions;

    // ── Local storage ─────────────────────────────────────────────────────
    std::string downloadDir{
        "/var/lib/firmware_updater"}; ///< Where binaries are stored

    // ── Download ──────────────────────────────────────────────────────────
    /// Per-file download timeout in seconds.  Firmware tarballs on slow BMC
    /// links can take many minutes; 1800 s (30 min) is a safe upper bound.
    int downloadTimeoutSeconds{1800};
};

// ── JSON de-serialisation ─────────────────────────────────────────────────

inline void from_json(const nlohmann::json& j, ServerAuth& a)
{
    a.username = j.value("username", "");
    a.password = j.value("password", "");
}

inline void from_json(const nlohmann::json& j, AppConfig& c)
{
    c.serverHost = j.at("serverHost").get<std::string>();
    c.serverPort = j.value("serverPort", "443");
    c.cataloguePath = j.value("cataloguePath", "/firmware/catalogue.json");
    if (j.contains("auth"))
    {
        c.auth = j["auth"].get<ServerAuth>();
    }
    c.pollIntervalSeconds = j.value("pollIntervalSeconds", 3600);
    c.pollAtStartup = j.value("pollAtStartup", true);
    if (j.contains("deniedVersions"))
    {
        c.deniedVersions = j["deniedVersions"].get<std::vector<std::string>>();
    }
    if (j.contains("allowedVersions"))
    {
        c.allowedVersions =
            j["allowedVersions"].get<std::vector<std::string>>();
    }
    c.downloadDir = j.value("downloadDir", "/var/lib/firmware_updater");
    c.downloadTimeoutSeconds = j.value("downloadTimeoutSeconds", 1800);
}

inline void from_json(const nlohmann::json& j, FirmwareEntry& e)
{
    e.version = j.at("version").get<std::string>();
    e.imageUrl = j.at("imageUrl").get<std::string>();
    e.checksum = j.value("checksum", "");
    e.releaseDate = j.value("releaseDate", "");
    e.description = j.value("description", "");
}

/**
 * @brief Load application configuration from a JSON file on disk.
 *
 * @param path  Filesystem path to the config file.
 * @return AppConfig on success, or an error string.
 */
inline std::expected<AppConfig, std::string> loadConfig(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs)
    {
        return std::unexpected(std::string("Cannot open config file: ") + path);
    }
    try
    {
        nlohmann::json j = nlohmann::json::parse(ifs);
        return j.get<AppConfig>();
    }
    catch (const std::exception& e)
    {
        return std::unexpected(std::string("Config parse error: ") + e.what());
    }
}

} // namespace firmware_updater
