#pragma once
#include "fw_updater_config.hpp"
#include "logger.hpp"
#include "sdbus_calls.hpp"
#include "webclient.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

namespace firmware_updater
{

/**
 * @brief Parse a semantic version string into a comparable tuple.
 *
 * Supports "MAJOR.MINOR.PATCH" and "MAJOR.MINOR" forms.  Any suffix
 * after the numeric part (e.g. "-rc1") is ignored for comparison.
 */
inline std::tuple<int, int, int> parseVersion(const std::string& ver)
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::sscanf(ver.c_str(), "%d.%d.%d", &major, &minor, &patch);
    return {major, minor, patch};
}

inline bool isNewerThan(const std::string& candidate,
                        const std::string& current)
{
    return parseVersion(candidate) > parseVersion(current);
}

/**
 * @brief Decide whether a candidate version is permitted for download.
 *
 * Rules (in evaluation order):
 *  1. If deniedVersions is non-empty and candidate is in it → denied.
 *  2. If allowedVersions is non-empty and candidate is NOT in it → denied.
 *  3. Otherwise → allowed.
 */
inline bool isAllowedVersion(const std::string& candidate, const AppConfig& cfg)
{
    if (!cfg.deniedVersions.empty())
    {
        if (std::ranges::find(cfg.deniedVersions, candidate) !=
            cfg.deniedVersions.end())
        {
            LOG_INFO("Version {} is in denied list — skipping", candidate);
            return false;
        }
    }
    if (!cfg.allowedVersions.empty())
    {
        if (std::ranges::find(cfg.allowedVersions, candidate) ==
            cfg.allowedVersions.end())
        {
            LOG_INFO("Version {} not in allowed list — skipping", candidate);
            return false;
        }
    }
    return true;
}

/**
 * @brief Fetch the firmware catalogue from the remote update server.
 *
 * Uses a plain WebClient (no Redfish session token) — bmcshell's firmware
 * endpoint is a simple HTTPS GET, not a Redfish-authenticated resource.
 *
 * @param ioc  Boost.Asio io_context.
 * @param cfg  Application configuration (host, port, cataloguePath).
 * @return     Pair of [error_code, catalogue entries].
 */
inline reactor::AwaitableResult<std::vector<FirmwareEntry>> fetchCatalogue(
    reactor::net::io_context& ioc, const AppConfig& cfg)
{
    reactor::ssl::context sslCtx(reactor::ssl::context::tlsv12_client);
    sslCtx.set_default_verify_paths();
    sslCtx.set_verify_mode(reactor::ssl::verify_none);

    reactor::WebClient<reactor::beast::tcp_stream> client(ioc, sslCtx);
    client.withHost(cfg.serverHost)
        .withPort(cfg.serverPort)
        .withMethod(reactor::http::verb::get)
        .withTarget(cfg.cataloguePath)
        .withRetries(3);

    LOG_INFO("Fetching catalogue from {}:{}{}", cfg.serverHost, cfg.serverPort,
             cfg.cataloguePath);

    auto [ec, res] = co_await client.execute<reactor::Response>();
    if (ec)
    {
        LOG_ERROR("Failed to fetch FW catalogue: {}", ec.message());
        co_return std::make_tuple(ec, std::vector<FirmwareEntry>{});
    }
    if (res.result() != reactor::http::status::ok)
    {
        LOG_ERROR("Catalogue request returned HTTP {}",
                  static_cast<int>(res.result()));
        co_return std::make_tuple(boost::system::errc::make_error_code(
                                      boost::system::errc::protocol_error),
                                  std::vector<FirmwareEntry>{});
    }

    try
    {
        auto j = nlohmann::json::parse(res.body());
        auto entries = j.get<std::vector<FirmwareEntry>>();
        co_return std::make_tuple(boost::system::error_code{},
                                  std::move(entries));
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Failed to parse FW catalogue JSON: {}", e.what());
        co_return std::make_tuple(boost::system::errc::make_error_code(
                                      boost::system::errc::invalid_argument),
                                  std::vector<FirmwareEntry>{});
    }
}

/**
 * @brief Read the currently running firmware version from D-Bus.
 *
 * Calls GetManagedObjects directly on xyz.openbmc_project.Software.BMC.Updater
 * — no object mapper involved.  Scans the returned objects for the one whose
 * xyz.openbmc_project.Software.Activation / Activation property equals
 * Activations.Active and returns its Version string.
 *
 * @param conn  Active sdbusplus connection.
 * @return      Pair of [error_code, version string].
 */
inline reactor::AwaitableResult<std::string> getCurrentFwVersion(
    sdbusplus::asio::connection& conn)
{
    constexpr auto updaterService = "xyz.openbmc_project.Software.BMC.Updater";
    constexpr auto softwarePath = "/xyz/openbmc_project/software";
    constexpr auto activationIface = "xyz.openbmc_project.Software.Activation";
    constexpr auto versionIface = "xyz.openbmc_project.Software.Version";
    constexpr auto activeState =
        "xyz.openbmc_project.Software.Activation.Activations.Active";

    // ManagedObjects: path → interface → property → variant
    using PropVariant = std::variant<std::string>;
    using IfaceProps = std::map<std::string, PropVariant>;
    using ManagedObjects = std::map<sdbusplus::message::object_path,
                                    std::map<std::string, IfaceProps>>;

    auto [ec, objects] = co_await reactor::getManagedObjects<ManagedObjects>(
        conn, updaterService, sdbusplus::message::object_path{softwarePath});

    if (ec)
    {
        LOG_WARNING("GetManagedObjects failed on {}: {}", updaterService,
                    ec.message());
        co_return std::make_tuple(ec, std::string{});
    }

    for (const auto& [objPath, ifaces] : objects)
    {
        // Must have both the Activation and Version interfaces.
        auto actIt = ifaces.find(activationIface);
        auto verIt = ifaces.find(versionIface);
        if (actIt == ifaces.end() || verIt == ifaces.end())
        {
            continue;
        }

        // Check Activation property.
        auto actPropIt = actIt->second.find("Activation");
        if (actPropIt == actIt->second.end())
        {
            continue;
        }
        const auto* actVal = std::get_if<std::string>(&actPropIt->second);
        if (!actVal || *actVal != activeState)
        {
            continue;
        }

        // Read Version property.
        auto verPropIt = verIt->second.find("Version");
        if (verPropIt == verIt->second.end())
        {
            LOG_ERROR("Active object {} has no Version property",
                      std::string(objPath));
            co_return std::make_tuple(
                boost::system::errc::make_error_code(
                    boost::system::errc::no_such_file_or_directory),
                std::string{});
        }
        const auto* verVal = std::get_if<std::string>(&verPropIt->second);
        if (!verVal)
        {
            LOG_ERROR("Version property on {} has unexpected type",
                      std::string(objPath));
            co_return std::make_tuple(
                boost::system::errc::make_error_code(
                    boost::system::errc::invalid_argument),
                std::string{});
        }

        LOG_INFO("Active firmware object: {}  version: {}",
                 std::string(objPath), *verVal);
        co_return std::make_tuple(boost::system::error_code{}, *verVal);
    }

    LOG_WARNING("No active firmware image found under {}", softwarePath);
    co_return std::make_tuple(
        boost::system::errc::make_error_code(
            boost::system::errc::no_such_file_or_directory),
        std::string{});
}

/**
 * @brief Select the single latest catalogue entry that is newer than the
 * running version and permitted by policy.
 *
 * Only downloading one image at a time avoids filling up disk and reduces
 * load on the update server.  The operator can re-run the check after
 * installation to pick up any further newer versions.
 *
 * @param catalogue   All entries returned by the remote server.
 * @param currentVer  Currently running firmware version.
 * @param cfg         Application configuration (allow/deny lists).
 * @return            At most one entry — the newest eligible candidate.
 */
inline std::vector<FirmwareEntry> selectCandidates(
    const std::vector<FirmwareEntry>& catalogue, const std::string& currentVer,
    const AppConfig& cfg)
{
    std::vector<FirmwareEntry> candidates;
    for (const auto& entry : catalogue)
    {
        if (isNewerThan(entry.version, currentVer) &&
            isAllowedVersion(entry.version, cfg))
        {
            candidates.push_back(entry);
        }
    }
    if (candidates.empty())
    {
        return {};
    }
    // Return only the newest eligible candidate.
    auto best = std::ranges::max_element(
        candidates, [](const FirmwareEntry& a, const FirmwareEntry& b) {
            return parseVersion(a.version) < parseVersion(b.version);
        });
    return {*best};
}

} // namespace firmware_updater
