#pragma once
#include <systemd/sd-bus.h>

#include <sdbusplus/sdbus.hpp>
#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Attestation/SecureExchange/common.hpp>

namespace sdbusplus::server::xyz::openbmc_project::attestation
{

class SecureExchange :
    public sdbusplus::common::xyz::openbmc_project::attestation::SecureExchange
{
  public:
    /* Define all of the basic class operations:
     *     Not allowed:
     *         - Default constructor to avoid nullptrs.
     *         - Copy operations due to internal unique_ptr.
     *         - Move operations due to 'this' being registered as the
     *           'context' with sdbus.
     *     Allowed:
     *         - Destructor.
     */
    SecureExchange() = delete;
    SecureExchange(const SecureExchange&) = delete;
    SecureExchange& operator=(const SecureExchange&) = delete;
    SecureExchange(SecureExchange&&) = delete;
    SecureExchange& operator=(SecureExchange&&) = delete;
    virtual ~SecureExchange() = default;

    /** @brief Constructor to put object onto bus at a dbus path.
     *  @param[in] bus - Bus to attach to.
     *  @param[in] path - Path to attach at.
     */
    SecureExchange(bus_t& bus, const char* path) :
        _xyz_openbmc_project_attestation_SecureExchange_interface(
            bus, path, interface, _vtable, this),
        _sdbusplus_bus(bus)
    {}

    /** @brief Implementation for ExchangeAppData
     *  Perform application data exchange over the active SPDM secure session.
     *  The session must already be established and authenticated.
     */
    virtual void exchangeAppData() = 0;

    /** @brief Emit interface added */
    void emit_added()
    {
        _xyz_openbmc_project_attestation_SecureExchange_interface.emit_added();
    }

    /** @brief Emit interface removed */
    void emit_removed()
    {
        _xyz_openbmc_project_attestation_SecureExchange_interface
            .emit_removed();
    }

    /** @return the bus instance */
    bus_t& get_bus()
    {
        return _sdbusplus_bus;
    }

  private:
    /** @brief sd-bus callback for ExchangeAppData
     */
    static int _callback_ExchangeAppData(sd_bus_message*, void*, sd_bus_error*);

    static const vtable_t _vtable[];
    sdbusplus::server::interface_t
        _xyz_openbmc_project_attestation_SecureExchange_interface;
    bus_t& _sdbusplus_bus;
};

} // namespace sdbusplus::server::xyz::openbmc_project::attestation

#ifndef SDBUSPP_REMOVE_DEPRECATED_NAMESPACE
namespace sdbusplus::xyz::openbmc_project::Attestation::server
{

using sdbusplus::server::xyz::openbmc_project::attestation::SecureExchange;

} // namespace sdbusplus::xyz::openbmc_project::Attestation::server
#endif
