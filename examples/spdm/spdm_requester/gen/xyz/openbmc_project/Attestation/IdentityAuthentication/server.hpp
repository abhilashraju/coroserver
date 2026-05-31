#pragma once
#include <limits>
#include <map>
#include <sdbusplus/sdbus.hpp>
#include <sdbusplus/server.hpp>
#include <string>
#include <systemd/sd-bus.h>

#include <xyz/openbmc_project/Attestation/IdentityAuthentication/common.hpp>

namespace sdbusplus::server::xyz::openbmc_project::attestation
{

class IdentityAuthentication :
    public sdbusplus::common::xyz::openbmc_project::attestation::IdentityAuthentication
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
        IdentityAuthentication() = delete;
        IdentityAuthentication(const IdentityAuthentication&) = delete;
        IdentityAuthentication& operator=(const IdentityAuthentication&) = delete;
        IdentityAuthentication(IdentityAuthentication&&) = delete;
        IdentityAuthentication& operator=(IdentityAuthentication&&) = delete;
        virtual ~IdentityAuthentication() = default;

        /** @brief Constructor to put object onto bus at a dbus path.
         *  @param[in] bus - Bus to attach to.
         *  @param[in] path - Path to attach at.
         */
        IdentityAuthentication(bus_t& bus, const char* path) :
            _xyz_openbmc_project_attestation_IdentityAuthentication_interface(
                bus, path, interface, _vtable, this),
            _sdbusplus_bus(bus) {}

        /** @brief Constructor to initialize the object from a map of
         *         properties.
         *
         *  @param[in] bus - Bus to attach to.
         *  @param[in] path - Path to attach at.
         *  @param[in] vals - Map of property name to value for initialization.
         */
        IdentityAuthentication(bus_t& bus, const char* path,
                     const std::map<std::string, PropertiesVariant>& vals,
                     bool skipSignal = false) :
            IdentityAuthentication(bus, path)
        {
            for (const auto& v : vals)
            {
                setPropertyByName(v.first, v.second, skipSignal);
            }
        }

        /** Get value of ResponderVerificationStatus */
        virtual VerificationStatus responderVerificationStatus() const;
        /** Set value of ResponderVerificationStatus with option to skip sending signal */
        virtual VerificationStatus responderVerificationStatus(VerificationStatus value,
               bool skipSignal);
        /** Set value of ResponderVerificationStatus */
        virtual VerificationStatus responderVerificationStatus(VerificationStatus value);

        /** @brief Sets a property by name.
         *  @param[in] _name - A string representation of the property name.
         *  @param[in] val - A variant containing the value to set.
         */
        void setPropertyByName(const std::string& _name,
                               const PropertiesVariant& val,
                               bool skipSignal = false);

        /** @brief Gets a property by name.
         *  @param[in] _name - A string representation of the property name.
         *  @return - A variant containing the value of the property.
         */
        PropertiesVariant getPropertyByName(const std::string& _name);



        /** @brief Emit interface added */
        void emit_added()
        {
            _xyz_openbmc_project_attestation_IdentityAuthentication_interface.emit_added();
        }

        /** @brief Emit interface removed */
        void emit_removed()
        {
            _xyz_openbmc_project_attestation_IdentityAuthentication_interface.emit_removed();
        }

        /** @return the bus instance */
        bus_t& get_bus()
        {
            return  _sdbusplus_bus;
        }

    private:

        /** @brief sd-bus callback for get-property 'ResponderVerificationStatus' */
        static int _callback_get_ResponderVerificationStatus(
            sd_bus*, const char*, const char*, const char*,
            sd_bus_message*, void*, sd_bus_error*);
        /** @brief sd-bus callback for set-property 'ResponderVerificationStatus' */
        static int _callback_set_ResponderVerificationStatus(
            sd_bus*, const char*, const char*, const char*,
            sd_bus_message*, void*, sd_bus_error*);

        static const vtable_t _vtable[];
        sdbusplus::server::interface_t
                _xyz_openbmc_project_attestation_IdentityAuthentication_interface;
        bus_t&  _sdbusplus_bus;
        VerificationStatus _responderVerificationStatus = VerificationStatus::Unknown;


};

} // namespace sdbusplus::server::xyz::openbmc_project::attestation

#ifndef SDBUSPP_REMOVE_DEPRECATED_NAMESPACE
namespace sdbusplus::xyz::openbmc_project::Attestation::server {

using sdbusplus::server::xyz::openbmc_project::attestation::IdentityAuthentication;
using sdbusplus::common::xyz::openbmc_project::attestation::convertForMessage;

} // namespace sdbusplus::xyz::openbmc_project::Attestation::server
#endif

