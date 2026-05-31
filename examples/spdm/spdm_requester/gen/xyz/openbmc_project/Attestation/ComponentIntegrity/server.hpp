#pragma once
#include <limits>
#include <map>
#include <sdbusplus/sdbus.hpp>
#include <sdbusplus/server.hpp>
#include <string>
#include <systemd/sd-bus.h>

#include <xyz/openbmc_project/Attestation/ComponentIntegrity/common.hpp>

namespace sdbusplus::server::xyz::openbmc_project::attestation
{

class ComponentIntegrity :
    public sdbusplus::common::xyz::openbmc_project::attestation::ComponentIntegrity
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
        ComponentIntegrity() = delete;
        ComponentIntegrity(const ComponentIntegrity&) = delete;
        ComponentIntegrity& operator=(const ComponentIntegrity&) = delete;
        ComponentIntegrity(ComponentIntegrity&&) = delete;
        ComponentIntegrity& operator=(ComponentIntegrity&&) = delete;
        virtual ~ComponentIntegrity() = default;

        /** @brief Constructor to put object onto bus at a dbus path.
         *  @param[in] bus - Bus to attach to.
         *  @param[in] path - Path to attach at.
         */
        ComponentIntegrity(bus_t& bus, const char* path) :
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface(
                bus, path, interface, _vtable, this),
            _sdbusplus_bus(bus) {}

        /** @brief Constructor to initialize the object from a map of
         *         properties.
         *
         *  @param[in] bus - Bus to attach to.
         *  @param[in] path - Path to attach at.
         *  @param[in] vals - Map of property name to value for initialization.
         */
        ComponentIntegrity(bus_t& bus, const char* path,
                     const std::map<std::string, PropertiesVariant>& vals,
                     bool skipSignal = false) :
            ComponentIntegrity(bus, path)
        {
            for (const auto& v : vals)
            {
                setPropertyByName(v.first, v.second, skipSignal);
            }
        }

        /** @brief Implementation for SetProvisioned
         *  Set the provisioned state of the component. This method allows updating the provisioning status of a component that supports security protocols.
         *
         *  @param[in] provisioned - The provisioned state to set. True indicates the component is provisioned, false indicates it is not provisioned.
         *
         *  @return success[bool] - Returns true if the provisioned state was successfully set, false otherwise.
         */
        virtual bool setProvisioned(
            bool provisioned) = 0;
        /** Get value of Enabled */
        virtual bool enabled() const;
        /** Set value of Enabled with option to skip sending signal */
        virtual bool enabled(bool value,
               bool skipSignal);
        /** Set value of Enabled */
        virtual bool enabled(bool value);
        /** Get value of Type */
        virtual SecurityTechnologyType type() const;
        /** Set value of Type with option to skip sending signal */
        virtual SecurityTechnologyType type(SecurityTechnologyType value,
               bool skipSignal);
        /** Set value of Type */
        virtual SecurityTechnologyType type(SecurityTechnologyType value);
        /** Get value of TypeVersion */
        virtual std::string typeVersion() const;
        /** Set value of TypeVersion with option to skip sending signal */
        virtual std::string typeVersion(std::string value,
               bool skipSignal);
        /** Set value of TypeVersion */
        virtual std::string typeVersion(std::string value);
        /** Get value of LastUpdated */
        virtual uint64_t lastUpdated() const;
        /** Set value of LastUpdated with option to skip sending signal */
        virtual uint64_t lastUpdated(uint64_t value,
               bool skipSignal);
        /** Set value of LastUpdated */
        virtual uint64_t lastUpdated(uint64_t value);

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
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.emit_added();
        }

        /** @brief Emit interface removed */
        void emit_removed()
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.emit_removed();
        }

        /** @return the bus instance */
        bus_t& get_bus()
        {
            return  _sdbusplus_bus;
        }

    private:
        /** @brief sd-bus callback for SetProvisioned
         */
        static int _callback_SetProvisioned(
            sd_bus_message*, void*, sd_bus_error*);

        /** @brief sd-bus callback for get-property 'Enabled' */
        static int _callback_get_Enabled(
            sd_bus*, const char*, const char*, const char*,
            sd_bus_message*, void*, sd_bus_error*);
        /** @brief sd-bus callback for set-property 'Enabled' */
        static int _callback_set_Enabled(
            sd_bus*, const char*, const char*, const char*,
            sd_bus_message*, void*, sd_bus_error*);

        /** @brief sd-bus callback for get-property 'Type' */
        static int _callback_get_Type(
            sd_bus*, const char*, const char*, const char*,
            sd_bus_message*, void*, sd_bus_error*);

        /** @brief sd-bus callback for get-property 'TypeVersion' */
        static int _callback_get_TypeVersion(
            sd_bus*, const char*, const char*, const char*,
            sd_bus_message*, void*, sd_bus_error*);

        /** @brief sd-bus callback for get-property 'LastUpdated' */
        static int _callback_get_LastUpdated(
            sd_bus*, const char*, const char*, const char*,
            sd_bus_message*, void*, sd_bus_error*);

        static const vtable_t _vtable[];
        sdbusplus::server::interface_t
                _xyz_openbmc_project_attestation_ComponentIntegrity_interface;
        bus_t&  _sdbusplus_bus;
        bool _enabled = false;

        SecurityTechnologyType _type = SecurityTechnologyType::Unknown;

        std::string _typeVersion{};

        uint64_t _lastUpdated{};


};

} // namespace sdbusplus::server::xyz::openbmc_project::attestation

#ifndef SDBUSPP_REMOVE_DEPRECATED_NAMESPACE
namespace sdbusplus::xyz::openbmc_project::Attestation::server {

using sdbusplus::server::xyz::openbmc_project::attestation::ComponentIntegrity;
using sdbusplus::common::xyz::openbmc_project::attestation::convertForMessage;

} // namespace sdbusplus::xyz::openbmc_project::Attestation::server
#endif

