#pragma once
#include "dbusproperty_watcher.hpp"
#include "fw_downloader.hpp"
#include "fw_updater_config.hpp"
#include "logger.hpp"
#include "sdbus_calls.hpp"

#include <nlohmann/json.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace firmware_updater
{

// ── D-Bus constants ────────────────────────────────────────────────────────

constexpr auto fwUpdaterService = "xyz.openbmc_project.FirmwareUpdater";
constexpr auto fwUpdaterBasePath = "/xyz/openbmc_project/firmware_updater";
constexpr auto fwManagerInterface =
    "xyz.openbmc_project.FirmwareUpdater.Manager";
constexpr auto fwObjectInterface = "xyz.openbmc_project.FirmwareUpdater.Image";

// ── FirmwareRecord (per-image D-Bus sub-object) ────────────────────────────

/**
 * @brief Holds the live D-Bus interface for a single downloaded image.
 *
 * Object path: /xyz/openbmc_project/firmware_updater/images/<version>
 * Interface  : xyz.openbmc_project.FirmwareUpdater.Image
 * Properties : Version, LocalPath, Checksum, ReleaseDate, Description
 */
struct FirmwareRecord
{
    FirmwareEntry entry;
    std::string localPath;
    std::shared_ptr<sdbusplus::asio::dbus_interface> iface;

    FirmwareRecord(sdbusplus::asio::object_server& objServer,
                   const FirmwareEntry& e, const std::string& path) :
        entry(e), localPath(path)
    {
        std::string objPath =
            std::string(fwUpdaterBasePath) + "/images/" + sanitise(e.version);

        iface = objServer.add_interface(objPath, fwObjectInterface);
        iface->register_property("Version", e.version);
        iface->register_property("LocalPath", path);
        iface->register_property("Checksum", e.checksum);
        iface->register_property("ReleaseDate", e.releaseDate);
        iface->register_property("Description", e.description);
        iface->initialize();

        LOG_INFO("Registered FW image D-Bus object: {}", objPath);
    }

  private:
    // Replace '.' with '_' so the version is a valid D-Bus path component.
    static std::string sanitise(const std::string& version)
    {
        std::string s = version;
        std::ranges::replace(s, '.', '_');
        return s;
    }
};

// ── DbusFirewareManager ────────────────────────────────────────────────────

/**
 * @brief Central D-Bus object that manages downloaded firmware images.
 *
 * Registers the service-level object at fwUpdaterBasePath and exposes:
 *
 *  Methods
 *  ───────
 *  ListFirmware() → array of (version, localPath, checksum, releaseDate,
 *                             description)
 *      Returns all firmware images that have been successfully downloaded.
 *
 *  Install(version: string) → void
 *      Triggers installation of a previously downloaded image.
 *      Invokes the BMC software-update D-Bus path exposed by
 *      xyz.openbmc_project.Software.Update (placeholder — concrete
 *      implementation depends on the BMC image layer).
 *
 *  Properties
 *  ──────────
 *  AvailableFirmware : array<string>
 *      Version strings of all downloaded images, kept live via
 *      updateAvailableFirmware().
 *
 *  Signals
 *  ───────
 *  FirmwareAvailable(version: string, localPath: string)
 *      Emitted once per newly downloaded image.  bmcweb / graphql-server
 *      listen for this signal to push Redfish event notifications.
 */
class DbusFirmwareManager
{
  public:
    DbusFirmwareManager(sdbusplus::asio::connection& conn,
                        sdbusplus::asio::object_server& objServer) :
        conn_(conn), objServer_(objServer)
    {
        managerIface_ =
            objServer_.add_interface(fwUpdaterBasePath, fwManagerInterface);

        // ── AvailableFirmware property ────────────────────────────────────
        managerIface_->register_property("AvailableFirmware",
                                         std::vector<std::string>{});

        // ── ListFirmware method ───────────────────────────────────────────
        // Returns: array of structs (version, localPath, checksum,
        //          releaseDate, description)
        using FwTuple = std::tuple<std::string, std::string, std::string,
                                   std::string, std::string>;
        managerIface_->register_method(
            "ListFirmware", [this]() -> std::vector<FwTuple> {
                std::vector<FwTuple> result;
                result.reserve(records_.size());
                for (const auto& [ver, rec] : records_)
                {
                    result.emplace_back(
                        rec->entry.version, rec->localPath, rec->entry.checksum,
                        rec->entry.releaseDate, rec->entry.description);
                }
                return result;
            });

        // ── Install method ────────────────────────────────────────────────
        managerIface_->register_method(
            "Install",
            [this](const std::string& version) { triggerInstall(version); });

        // ── CheckNow method ───────────────────────────────────────────────
        // Triggers an immediate firmware check cycle outside the normal
        // polling interval.  The callback is wired by main() after all
        // objects are constructed so the coroutine context is available.
        managerIface_->register_method("CheckNow", [this]() {
            if (checkNowCallback_)
            {
                checkNowCallback_();
                LOG_INFO("CheckNow: manual firmware check triggered");
            }
            else
            {
                LOG_WARNING("CheckNow called before callback was registered");
            }
        });

        managerIface_->initialize();

        LOG_INFO("DbusFirmwareManager registered at {}", fwUpdaterBasePath);
    }

    ~DbusFirmwareManager()
    {
        for (auto& [ver, rec] : records_)
        {
            if (rec && rec->iface)
            {
                objServer_.remove_interface(rec->iface);
            }
        }
        if (managerIface_)
        {
            objServer_.remove_interface(managerIface_);
        }
    }

    DbusFirmwareManager(const DbusFirmwareManager&) = delete;
    DbusFirmwareManager& operator=(const DbusFirmwareManager&) = delete;
    DbusFirmwareManager(DbusFirmwareManager&&) = delete;
    DbusFirmwareManager& operator=(DbusFirmwareManager&&) = delete;

    /**
     * @brief Register a newly downloaded firmware image.
     *
     * Creates a per-image sub-object, updates the AvailableFirmware
     * property, and emits the FirmwareAvailable D-Bus signal.
     *
     * Must be called from outside a coroutine stack (sdbusplus object
     * registration flushes synchronously).  Use net::post() when calling
     * from a coroutine.
     *
     * @param result  Successful DownloadResult from downloadFirmware().
     * @param entry   The FirmwareEntry that was downloaded.
     */
    void onFirmwareDownloaded(const DownloadResult& result,
                              const FirmwareEntry& entry)
    {
        if (result.ec)
        {
            return; // Download failed — nothing to register.
        }

        const std::string& ver = entry.version;

        if (records_.contains(ver))
        {
            LOG_INFO("Firmware {} already registered — skipping", ver);
            return;
        }

        // Create per-image D-Bus object.
        records_.emplace(ver, std::make_unique<FirmwareRecord>(
                                  objServer_, entry, result.localPath));

        // Update the AvailableFirmware property.
        updateAvailableFirmware();

        // Emit FirmwareAvailable signal.
        emitFirmwareAvailable(ver, result.localPath);
    }

    /**
     * @brief Remove a firmware record (e.g. after successful installation).
     */
    void removeFirmware(const std::string& version)
    {
        auto it = records_.find(version);
        if (it == records_.end())
        {
            return;
        }
        if (it->second && it->second->iface)
        {
            objServer_.remove_interface(it->second->iface);
        }
        records_.erase(it);
        updateAvailableFirmware();
        LOG_INFO("Removed FW record for version {}", version);
    }

    /**
     * @brief Check whether a version has already been downloaded.
     */
    bool hasVersion(const std::string& version) const
    {
        return records_.contains(version);
    }

  private:
    // ── Helpers ───────────────────────────────────────────────────────────

    void updateAvailableFirmware()
    {
        std::vector<std::string> versions;
        versions.reserve(records_.size());
        for (const auto& [ver, _] : records_)
        {
            versions.push_back(ver);
        }
        managerIface_->set_property("AvailableFirmware", versions);
    }

    /**
     * @brief Emit the xyz.openbmc_project.FirmwareUpdater.Manager
     *        FirmwareAvailable signal.
     *
     * Signal signature: (version: s, localPath: s)
     *
     * bmcweb and graphql-server subscribe to this signal to trigger
     * Redfish UpdateService / EventService notifications.
     */
    void emitFirmwareAvailable(const std::string& version,
                               const std::string& localPath)
    {
        auto msg = conn_.new_signal(fwUpdaterBasePath, fwManagerInterface,
                                    "FirmwareAvailable");
        msg.append(version, localPath);
        msg.signal_send();
        LOG_INFO("Emitted FirmwareAvailable signal: version={} path={}",
                 version, localPath);
    }

    void triggerInstall(const std::string& version)
    {
        auto it = records_.find(version);
        if (it == records_.end())
        {
            LOG_ERROR("Install requested for unknown version {}", version);
            throw sdbusplus::exception::SdBusError(ENOENT, "Version not found");
        }

        const std::string& localPath = it->second->localPath;
        LOG_INFO("Install requested: version={} path={}", version, localPath);

        boost::asio::co_spawn(
            conn_.get_io_context(),
            [this, version, localPath]() -> boost::asio::awaitable<void> {
                co_await invokeSoftwareUpdate(version, localPath);
            }(),
            boost::asio::detached);
    }

    boost::asio::awaitable<void> invokeSoftwareUpdate(
        const std::string& version, const std::string& localPath)
    {
        namespace fs = std::filesystem;
        constexpr auto activationIface =
            "xyz.openbmc_project.Software.Activation";
        constexpr auto softwareRoot = "/xyz/openbmc_project/software";

        // ── Step 1: copy image into /tmp/images/
        // ────────────────────────────── /tmp/images is always present
        // (symlinked by the BMC rootfs). phosphor-software-manager watches it
        // via inotify, validates the image and emits InterfacesAdded on
        // softwareRoot when ready.
        constexpr auto stagingDir = "/tmp/images";
        fs::path dest = fs::path(stagingDir) / fs::path(localPath).filename();
        std::error_code fec;
        fs::copy_file(localPath, dest, fs::copy_options::overwrite_existing,
                      fec);
        if (fec)
        {
            LOG_ERROR("Failed to stage {} -> {}: {}", localPath, dest.string(),
                      fec.message());
            co_return;
        }
        LOG_INFO("Staged firmware to {}", dest.string());

        // ── Step 2: wait for InterfacesAdded via DbusSignalWatcher ───────────
        // watchOnce suspends until the signal fires or the 120s timeout
        // expires.
        auto connPtr =
            std::shared_ptr<sdbusplus::asio::connection>(&conn_, [](auto*) {});
        auto signalWatcher =
            reactor::DbusSignalWatcher<sdbusplus::message_t>::create(connPtr);
        signalWatcher->interfacesAddedAtPath(softwareRoot);

        auto msgOpt =
            co_await signalWatcher->watchOnce(std::chrono::seconds(120));
        if (!msgOpt)
        {
            LOG_ERROR("Timed out waiting for activation object for version={}",
                      version);
            co_return;
        }

        sdbusplus::message::object_path newObjPath;
        std::map<std::string,
                 std::map<std::string, std::variant<std::string, bool, uint8_t,
                                                    uint32_t, uint64_t>>>
            interfaces;
        msgOpt->read(newObjPath, interfaces);

        if (!interfaces.contains(activationIface))
        {
            LOG_ERROR("InterfacesAdded on {} missing {}", newObjPath.str,
                      activationIface);
            co_return;
        }
        LOG_INFO("Activation object ready: {}", newObjPath.str);

        // ── Step 3: find owning service and set RequestedActivation
        // ───────────
        using ObjMap = std::map<std::string, std::vector<std::string>>;
        auto [objEc, objInfo] = co_await reactor::getObjects<ObjMap>(
            conn_, newObjPath.str, std::vector<std::string>{activationIface});
        if (objEc || objInfo.empty())
        {
            LOG_ERROR("Cannot find service for {}: {}", newObjPath.str,
                      objEc.message());
            co_return;
        }
        const std::string& service = objInfo.begin()->first;

        constexpr auto requestedActive =
            "xyz.openbmc_project.Software.Activation.RequestedActivations."
            "Active";
        auto [setEc] = co_await reactor::setProperty<std::string>(
            conn_, service, newObjPath.str, activationIface,
            "RequestedActivation", std::string{requestedActive});
        if (setEc)
        {
            LOG_ERROR("SetProperty(RequestedActivation) failed: {}",
                      setEc.message());
            co_return;
        }

        // ── Step 4: poll Activation property until terminal state
        // ───────────── watchOnce suspends for one property-change event (with
        // a per-call timeout).  Intermediate states like "Activating" keep us
        // looping; Active / Failed / Invalid exit.
        auto propWatcher = reactor::DbusPropertyWatcher<std::string>::create(
            connPtr, newObjPath.str, activationIface, "Activation");

        while (true)
        {
            auto stateOpt =
                co_await propWatcher->watchOnce(std::chrono::seconds(300));
            if (!stateOpt)
            {
                LOG_ERROR(
                    "Timed out waiting for activation state for version={}",
                    version);
                break;
            }
            LOG_INFO("Activation state for {}: {}", version, *stateOpt);
            if (stateOpt->ends_with("Active"))
            {
                LOG_INFO("Firmware {} activated successfully", version);
                removeFirmware(version);
                break;
            }
            if (stateOpt->ends_with("Failed") || stateOpt->ends_with("Invalid"))
            {
                LOG_ERROR("Firmware {} activation failed: {}", version,
                          *stateOpt);
                break;
            }
            // Intermediate state (e.g. Activating) — wait for the next change.
        }
    }

    // ── Members ───────────────────────────────────────────────────────────

    sdbusplus::asio::connection& conn_;
    sdbusplus::asio::object_server& objServer_;
    std::shared_ptr<sdbusplus::asio::dbus_interface> managerIface_;
    // version string → record
    std::map<std::string, std::unique_ptr<FirmwareRecord>> records_;
    // Set by main() to co_spawn a runCheckCycle when CheckNow is called.
    std::function<void()> checkNowCallback_;

  public:
    /**
     * @brief Wire the callback invoked by the CheckNow D-Bus method.
     *
     * Must be called after main() has constructed both the io_context and
     * the RedfishClient so the lambda can safely capture them by reference.
     *
     * @param cb  Callable with signature void() — typically a lambda that
     *            calls net::co_spawn(ioc, runCheckCycle(...), net::detached).
     */
    void setCheckNowCallback(std::function<void()> cb)
    {
        checkNowCallback_ = std::move(cb);
    }
};

} // namespace firmware_updater
