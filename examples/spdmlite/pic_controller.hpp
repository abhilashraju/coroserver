#pragma once
#include "sdbus_calls.hpp"
struct PicController
{
    std::shared_ptr<sdbusplus::asio::connection> conn;
    sdbusplus::asio::object_server dbusServer;
    std::shared_ptr<sdbusplus::asio::dbus_interface> iface;
    using PROVISIONING_STATE_HANDLER = std::function<void(bool)>;
    PROVISIONING_STATE_HANDLER provisioningStateHandler;
    static constexpr std::string_view busName = "xyz.openbmc_project.spdm";
    static constexpr std::string_view objPath = "/xyz/openbmc_project/spdm";
    static constexpr std::string_view interface =
        "xyz.openbmc_project.Provisioning.Status";
    PicController(std::shared_ptr<sdbusplus::asio::connection> conn,
                  PROVISIONING_STATE_HANDLER handler) :
        conn(conn), dbusServer(conn),
        provisioningStateHandler(std::move(handler))
    {
        iface = dbusServer.add_interface(objPath.data(), interface.data());
        // test generic properties

        iface->register_method("ClearProvisioningData", [this]() {
            clearProvisioningData();
        });

        iface->register_property(
            "ProvisioningState", false,
            std::bind_front(&PicController::setProvisioningState, this),
            std::bind_front(&PicController::getProvisioningState, this));

        iface->initialize();
    }
    void clearProvisioningData()
    {
        // This method would clear the provisioning data.
        // Implementation would depend on the specific requirements.
        LOG_INFO("Clearing provisioning data");
    }
    bool setProvisioningState(bool newstate, bool& currentstate)
    {
        if (currentstate == newstate)
        {
            LOG_INFO("Provisioning state is already set to {}", newstate);
            return false; // No change needed
        }
        currentstate = newstate;
        if (currentstate)
        {
            clearCertificates();
        }
        provisioningStateHandler(currentstate);
        return true; // Return true if successful
    }
    bool getProvisioningState(bool currentstate)
    {
        // This method would get the current provisioning state.
        // Implementation would depend on the specific requirements.
        LOG_INFO("Getting provisioning state: {}", currentstate);
        return currentstate; // Return the current state
    }
};
