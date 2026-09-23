#include "async_wait.hpp"
#include "command_line_parser.hpp"
#include "dbus_fw_manager.hpp"
#include "fw_downloader.hpp"
#include "fw_updater_config.hpp"
#include "fw_version_checker.hpp"
#include "logger.hpp"

#include <boost/asio.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <chrono>
#include <memory>
#include <string>

using namespace firmware_updater;
using namespace reactor;
using namespace std::chrono_literals;
namespace net = boost::asio;

// ── Check-and-download cycle ───────────────────────────────────────────────

/**
 * @brief Perform one firmware check-and-download cycle.
 *
 * Steps:
 *  1. Read the currently running firmware version from D-Bus.
 *  2. Fetch the firmware catalogue from the remote update server.
 *  3. Select candidates that are newer and policy-permitted.
 *  4. Download each candidate (skips already-downloaded images).
 *  5. Register each successful download with the DbusFirmwareManager.
 *
 * The registration step is posted to the io_context top-level to avoid
 * calling sdbusplus object-server APIs from inside a coroutine stack
 * (sdbusplus flushes sd_bus synchronously on registration, which would
 * deadlock the event loop).
 */
net::awaitable<void> runCheckCycle(sdbusplus::asio::connection& conn,
                                   DbusFirmwareManager& dbusManager,
                                   const AppConfig& cfg)
{
    auto executor = co_await net::this_coro::executor;
    auto& ioc = static_cast<net::io_context&>(executor.context());

    LOG_INFO("Starting firmware check cycle");

    // 1. Read current running version.
    auto [verEc, currentVer] = co_await getCurrentFwVersion(conn);
    if (verEc || currentVer.empty())
    {
        LOG_WARNING("Could not determine current FW version ({}), "
                    "skipping cycle",
                    verEc ? verEc.message() : "empty version");
        co_return;
    }
    LOG_INFO("Current firmware version: {}", currentVer);

    // 2. Fetch catalogue — plain HTTPS GET, no Redfish auth.
    auto [catEc, catalogue] = co_await fetchCatalogue(ioc, cfg);
    if (catEc)
    {
        LOG_ERROR("Catalogue fetch failed: {} — skipping cycle",
                  catEc.message());
        co_return;
    }
    LOG_INFO("Fetched {} catalogue entries from update server",
             catalogue.size());

    // 3. Select upgrade candidates.
    auto candidates = selectCandidates(catalogue, currentVer, cfg);
    if (candidates.empty())
    {
        LOG_INFO("No new firmware candidates — nothing to download");
        co_return;
    }
    LOG_INFO("{} firmware candidate(s) to download", candidates.size());

    // 4. Download each candidate.
    for (const auto& entry : candidates)
    {
        if (dbusManager.hasVersion(entry.version))
        {
            LOG_INFO("Version {} already registered — skipping download",
                     entry.version);
            continue;
        }

        auto result = co_await downloadFirmware(ioc, entry, cfg.downloadDir,
                                                cfg.downloadTimeoutSeconds);

        if (result.ec)
        {
            LOG_ERROR("Download failed for version {}: {}", entry.version,
                      result.ec.message());
            continue;
        }

        // 5. Register with the D-Bus manager.
        //
        // sdbusplus add_interface / initialize are not safe to call from
        // inside a coroutine because they flush sd_bus synchronously.
        // net::post schedules the registration at the top of the event loop
        // after the co_await stack has fully unwound.
        net::post(executor, [&dbusManager, result, entry]() mutable {
            dbusManager.onFirmwareDownloaded(result, entry);
        });
    }
}

// ── Periodic polling loop ─────────────────────────────────────────────────

/**
 * @brief Coroutine that drives periodic firmware checks.
 *
 * Runs one cycle at startup (if cfg.pollAtStartup is true), then sleeps
 * for cfg.pollIntervalSeconds between cycles.  A pollIntervalSeconds of 0
 * means startup-only (one-shot).
 */
net::awaitable<void> pollingLoop(sdbusplus::asio::connection& conn,
                                 DbusFirmwareManager& dbusManager,
                                 const AppConfig& cfg)
{
    auto executor = co_await net::this_coro::executor;

    if (cfg.pollAtStartup)
    {
        co_await runCheckCycle(conn, dbusManager, cfg);
    }

    if (cfg.pollIntervalSeconds <= 0)
    {
        LOG_INFO("One-shot mode — polling loop exiting");
        co_return;
    }

    while (true)
    {
        LOG_INFO("Next firmware check in {} seconds", cfg.pollIntervalSeconds);

        auto ec = co_await reactor::waitFor(
            executor, std::chrono::seconds(cfg.pollIntervalSeconds));

        if (ec == net::error::operation_aborted)
        {
            LOG_INFO("Polling loop cancelled — exiting");
            co_return;
        }

        co_await runCheckCycle(conn, dbusManager, cfg);
    }
}

// ── main ──────────────────────────────────────────────────────────────────

int main(int argc, const char* argv[])
{
    reactor::getLogger().setLogLevel(reactor::LogLevel::INFO);

    try
    {
        // ── Parse command line ────────────────────────────────────────────
        auto [configPath] =
            getArgs(parseCommandline(argc, argv), "--config,-c");

        if (!configPath.has_value())
        {
            LOG_ERROR("{}",
                      "Usage: firmware_updater -c <config.json>\n"
                      "\n"
                      "  config.json keys (see config/firmware_updater.json):\n"
                      "    serverHost, serverPort, cataloguePath,\n"
                      "    auth: { username, password },\n"
                      "    pollIntervalSeconds, pollAtStartup,\n"
                      "    deniedVersions, allowedVersions, downloadDir\n");
            return EXIT_FAILURE;
        }

        // ── Load configuration ────────────────────────────────────────────
        auto maybeCfg = loadConfig(std::string(*configPath));
        if (!maybeCfg)
        {
            LOG_ERROR("Configuration error: {}", maybeCfg.error());
            return EXIT_FAILURE;
        }
        const AppConfig& cfg = *maybeCfg;

        LOG_INFO("FirmwareUpdater starting — server={}:{} interval={}s",
                 cfg.serverHost, cfg.serverPort, cfg.pollIntervalSeconds);

        // ── Boost.Asio io_context ─────────────────────────────────────────
        net::io_context ioc;

        // ── D-Bus setup ───────────────────────────────────────────────────
        auto conn = std::make_shared<sdbusplus::asio::connection>(ioc);
        conn->request_name(fwUpdaterService);

        sdbusplus::asio::object_server objServer(conn);

        // The DbusFirmwareManager registers D-Bus objects/interfaces here,
        // before ioc.run() — safe from the main thread.
        DbusFirmwareManager dbusManager(*conn, objServer);

        // ── Spawn polling loop ────────────────────────────────────────────
        net::co_spawn(ioc, pollingLoop(*conn, dbusManager, cfg), net::detached);

        // ── Wire CheckNow D-Bus callback ──────────────────────────────────
        // Captured by reference — all objects outlive ioc.run().
        dbusManager.setCheckNowCallback([&ioc, &conn, &dbusManager, &cfg]() {
            net::co_spawn(ioc, runCheckCycle(*conn, dbusManager, cfg),
                          net::detached);
        });

        LOG_INFO("FirmwareUpdater D-Bus service running — objects at {}",
                 fwUpdaterBasePath);

        ioc.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Fatal: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
