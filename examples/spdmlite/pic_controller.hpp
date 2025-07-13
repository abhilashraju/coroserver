#pragma once
#include "sdbus_calls.hpp"
struct PicController
{
    std::shared_ptr<sdbusplus::asio::connection> conn;
    sdbusplus::asio::object_server dbusServer;
    std::shared_ptr<sdbusplus::asio::dbus_interface> iface;

    using PROVISIONING_STATE_HANDLER = std::function<void(bool)>;
    PROVISIONING_STATE_HANDLER provisioningStateHandler;
    static constexpr auto busName = "xyz.openbmc_project.spdm";
    static constexpr auto objPath = "/xyz/openbmc_project/spdm";
    static constexpr auto interface = "xyz.openbmc_project.Provisioning.Status";
    PicController(std::shared_ptr<sdbusplus::asio::connection> conn,
                  PROVISIONING_STATE_HANDLER handler) :
        conn(conn), dbusServer(conn),
        provisioningStateHandler(std::move(handler))
    {
        iface = dbusServer.add_interface(objPath, interface);
        // test generic properties

        iface->register_method("ClearProvisioningData", [this]() {
            clearProvisioningData();
        });
        iface->register_method("provision", [this]() { provision(); });

        iface->register_property(
            "ProvisioningState", false,
            std::bind_front(&PicController::setProvisioningState, this),
            std::bind_front(&PicController::getProvisioningState, this));

        iface->initialize();
    }
    void provision()
    {
        clearProvisioningData();
        provisioningStateHandler(true);
        LOG_INFO("Provisioning started");
    }
    void clearProvisioningData()
    {
        clearCertificates();
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
