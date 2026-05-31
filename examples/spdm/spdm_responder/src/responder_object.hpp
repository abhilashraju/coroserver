#pragma once

#include <sdbusplus/asio/object_server.hpp>

#include <memory>
#include <string>

class SpdmResponderObject
{
  public:
    SpdmResponderObject(std::shared_ptr<sdbusplus::asio::connection> conn,
                        const std::string& deviceId) :
        conn(conn), deviceId(deviceId), provisioned(false)
    {
        objectServer = std::make_unique<sdbusplus::asio::object_server>(conn);

        // Create the D-Bus object path:
        // /xyz/openbmc_project/Attestation/{deviceId}
        std::string objectPath =
            "/xyz/openbmc_project/spdm/responder/" + deviceId;

        // Create the interface
        iface = objectServer->add_interface(
            objectPath, "xyz.openbmc_project.Attestation.SpdmResponder");

        // Register the 'provisioned' property
        iface->register_property(
            "provisioned", provisioned,
            sdbusplus::asio::PropertyPermission::readWrite);

        // Initialize the interface
        iface->initialize();
    }

    ~SpdmResponderObject() = default;

    // Getter for provisioned property
    bool getProvisioned() const
    {
        return provisioned;
    }

    // Setter for provisioned property
    void setProvisioned(bool value)
    {
        provisioned = value;
        if (iface)
        {
            iface->set_property("provisioned", provisioned);
        }
    }

  private:
    std::shared_ptr<sdbusplus::asio::connection> conn;
    std::unique_ptr<sdbusplus::asio::object_server> objectServer;
    std::shared_ptr<sdbusplus::asio::dbus_interface> iface;
    std::string deviceId;
    bool provisioned;
};
