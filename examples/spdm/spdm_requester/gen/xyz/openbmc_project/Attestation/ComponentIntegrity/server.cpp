#include <exception>
#include <map>
#include <sdbusplus/sdbus.hpp>
#include <sdbusplus/sdbuspp_support/server.hpp>
#include <sdbusplus/server.hpp>
#include <string>
#include <tuple>

#include <xyz/openbmc_project/Attestation/ComponentIntegrity/server.hpp>

namespace sdbusplus::server::xyz::openbmc_project::attestation
{

int ComponentIntegrity::_callback_SetProvisioned(
        sd_bus_message* msg, void* context, sd_bus_error* error)
{
    auto o = static_cast<ComponentIntegrity*>(context);

    try
    {
        return sdbusplus::sdbuspp::method_callback(
                msg, o->get_bus().getInterface(), error,
                std::function(
                    [=](bool&& provisioned)
                    {
                        return o->setProvisioned(
                                provisioned);
                    }
                ));
    }
    catch (const std::exception&)
    {
        o->get_bus().set_current_exception(std::current_exception());
        return 1;
    }
}

namespace details
{
namespace ComponentIntegrity
{
static const auto _param_SetProvisioned =
        utility::tuple_to_array(message::types::type_id<
                bool>());
static const auto _return_SetProvisioned =
        utility::tuple_to_array(message::types::type_id<
                bool>());
}
}


auto ComponentIntegrity::enabled() const ->
        bool
{
    return _enabled;
}

int ComponentIntegrity::_callback_get_Enabled(
        sd_bus* /*bus*/, const char* /*path*/, const char* /*interface*/,
        const char* /*property*/, sd_bus_message* reply, void* context,
        sd_bus_error* error)
{
    auto o = static_cast<ComponentIntegrity*>(context);

    try
    {
        return sdbusplus::sdbuspp::property_callback(
                reply, o->get_bus().getInterface(), error,
                std::function(
                    [=]()
                    {
                        return o->enabled();
                    }
                ));
    }
    catch (const std::exception&)
    {
        o->get_bus().set_current_exception(std::current_exception());
        return 1;
    }
}

auto ComponentIntegrity::enabled(bool value,
                                         bool skipSignal) ->
        bool
{
    if (_enabled != value)
    {
        _enabled = value;
        if (!skipSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("Enabled");
        }
    }

    return _enabled;
}

auto ComponentIntegrity::enabled(bool val) ->
        bool
{
    return enabled(val, false);
}

int ComponentIntegrity::_callback_set_Enabled(
        sd_bus* /*bus*/, const char* /*path*/, const char* /*interface*/,
        const char* /*property*/, sd_bus_message* value, void* context,
        sd_bus_error* error)
{
    auto o = static_cast<ComponentIntegrity*>(context);

    try
    {
        return sdbusplus::sdbuspp::property_callback(
                value, o->get_bus().getInterface(), error,
                std::function(
                    [=](bool&& arg)
                    {
                        o->enabled(std::move(arg));
                    }
                ));
    }
    catch (const std::exception&)
    {
        o->get_bus().set_current_exception(std::current_exception());
        return 1;
    }
}

namespace details
{
namespace ComponentIntegrity
{
static const auto _property_Enabled =
    utility::tuple_to_array(message::types::type_id<
            bool>());
}
}

auto ComponentIntegrity::type() const ->
        SecurityTechnologyType
{
    return _type;
}

int ComponentIntegrity::_callback_get_Type(
        sd_bus* /*bus*/, const char* /*path*/, const char* /*interface*/,
        const char* /*property*/, sd_bus_message* reply, void* context,
        sd_bus_error* error)
{
    auto o = static_cast<ComponentIntegrity*>(context);

    try
    {
        return sdbusplus::sdbuspp::property_callback(
                reply, o->get_bus().getInterface(), error,
                std::function(
                    [=]()
                    {
                        return o->type();
                    }
                ));
    }
    catch (const std::exception&)
    {
        o->get_bus().set_current_exception(std::current_exception());
        return 1;
    }
}

auto ComponentIntegrity::type(SecurityTechnologyType value,
                                         bool skipSignal) ->
        SecurityTechnologyType
{
    if (_type != value)
    {
        _type = value;
        if (!skipSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("Type");
        }
    }

    return _type;
}

auto ComponentIntegrity::type(SecurityTechnologyType val) ->
        SecurityTechnologyType
{
    return type(val, false);
}


namespace details
{
namespace ComponentIntegrity
{
static const auto _property_Type =
    utility::tuple_to_array(message::types::type_id<
            sdbusplus::common::xyz::openbmc_project::attestation::ComponentIntegrity::SecurityTechnologyType>());
}
}

auto ComponentIntegrity::typeVersion() const ->
        std::string
{
    return _typeVersion;
}

int ComponentIntegrity::_callback_get_TypeVersion(
        sd_bus* /*bus*/, const char* /*path*/, const char* /*interface*/,
        const char* /*property*/, sd_bus_message* reply, void* context,
        sd_bus_error* error)
{
    auto o = static_cast<ComponentIntegrity*>(context);

    try
    {
        return sdbusplus::sdbuspp::property_callback(
                reply, o->get_bus().getInterface(), error,
                std::function(
                    [=]()
                    {
                        return o->typeVersion();
                    }
                ));
    }
    catch (const std::exception&)
    {
        o->get_bus().set_current_exception(std::current_exception());
        return 1;
    }
}

auto ComponentIntegrity::typeVersion(std::string value,
                                         bool skipSignal) ->
        std::string
{
    if (_typeVersion != value)
    {
        _typeVersion = value;
        if (!skipSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("TypeVersion");
        }
    }

    return _typeVersion;
}

auto ComponentIntegrity::typeVersion(std::string val) ->
        std::string
{
    return typeVersion(val, false);
}


namespace details
{
namespace ComponentIntegrity
{
static const auto _property_TypeVersion =
    utility::tuple_to_array(message::types::type_id<
            std::string>());
}
}

auto ComponentIntegrity::lastUpdated() const ->
        uint64_t
{
    return _lastUpdated;
}

int ComponentIntegrity::_callback_get_LastUpdated(
        sd_bus* /*bus*/, const char* /*path*/, const char* /*interface*/,
        const char* /*property*/, sd_bus_message* reply, void* context,
        sd_bus_error* error)
{
    auto o = static_cast<ComponentIntegrity*>(context);

    try
    {
        return sdbusplus::sdbuspp::property_callback(
                reply, o->get_bus().getInterface(), error,
                std::function(
                    [=]()
                    {
                        return o->lastUpdated();
                    }
                ));
    }
    catch (const std::exception&)
    {
        o->get_bus().set_current_exception(std::current_exception());
        return 1;
    }
}

auto ComponentIntegrity::lastUpdated(uint64_t value,
                                         bool skipSignal) ->
        uint64_t
{
    if (_lastUpdated != value)
    {
        _lastUpdated = value;
        if (!skipSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("LastUpdated");
        }
    }

    return _lastUpdated;
}

auto ComponentIntegrity::lastUpdated(uint64_t val) ->
        uint64_t
{
    return lastUpdated(val, false);
}


namespace details
{
namespace ComponentIntegrity
{
static const auto _property_LastUpdated =
    utility::tuple_to_array(message::types::type_id<
            uint64_t>());
}
}


void ComponentIntegrity::setPropertyByName(const std::string& _name,
                                     const PropertiesVariant& val,
                                     bool skipSignal)
{
    if (_name == "Enabled")
    {
        auto& v = std::get<bool>(val);
        enabled(v, skipSignal);
        return;
    }
    if (_name == "Type")
    {
        auto& v = std::get<SecurityTechnologyType>(val);
        type(v, skipSignal);
        return;
    }
    if (_name == "TypeVersion")
    {
        auto& v = std::get<std::string>(val);
        typeVersion(v, skipSignal);
        return;
    }
    if (_name == "LastUpdated")
    {
        auto& v = std::get<uint64_t>(val);
        lastUpdated(v, skipSignal);
        return;
    }
}

auto ComponentIntegrity::getPropertyByName(const std::string& _name) ->
        PropertiesVariant
{
    if (_name == "Enabled")
    {
        return enabled();
    }
    if (_name == "Type")
    {
        return type();
    }
    if (_name == "TypeVersion")
    {
        return typeVersion();
    }
    if (_name == "LastUpdated")
    {
        return lastUpdated();
    }

    return PropertiesVariant();
}



const vtable_t ComponentIntegrity::_vtable[] = {
    vtable::start(),

    vtable::method("SetProvisioned",
                   details::ComponentIntegrity::_param_SetProvisioned.data(),
                   details::ComponentIntegrity::_return_SetProvisioned.data(),
                   _callback_SetProvisioned),

    vtable::property("Enabled",
                     details::ComponentIntegrity::_property_Enabled.data(),
                     _callback_get_Enabled,
                     _callback_set_Enabled,
                     vtable::property_::emits_change),

    vtable::property("Type",
                     details::ComponentIntegrity::_property_Type.data(),
                     _callback_get_Type,
                     vtable::property_::emits_change),

    vtable::property("TypeVersion",
                     details::ComponentIntegrity::_property_TypeVersion.data(),
                     _callback_get_TypeVersion,
                     vtable::property_::emits_change),

    vtable::property("LastUpdated",
                     details::ComponentIntegrity::_property_LastUpdated.data(),
                     _callback_get_LastUpdated,
                     vtable::property_::emits_change),

    vtable::end()
};

} // namespace sdbusplus::server::xyz::openbmc_project::attestation

