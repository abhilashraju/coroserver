#include <exception>
#include <map>
#include <sdbusplus/sdbus.hpp>
#include <sdbusplus/sdbuspp_support/server.hpp>
#include <sdbusplus/server.hpp>
#include <string>
#include <tuple>

#include <xyz/openbmc_project/Attestation/IdentityAuthentication/server.hpp>

namespace sdbusplus::server::xyz::openbmc_project::attestation
{



auto IdentityAuthentication::responderVerificationStatus() const ->
        VerificationStatus
{
    return _responderVerificationStatus;
}

int IdentityAuthentication::_callback_get_ResponderVerificationStatus(
        sd_bus* /*bus*/, const char* /*path*/, const char* /*interface*/,
        const char* /*property*/, sd_bus_message* reply, void* context,
        sd_bus_error* error)
{
    auto o = static_cast<IdentityAuthentication*>(context);

    try
    {
        return sdbusplus::sdbuspp::property_callback(
                reply, o->get_bus().getInterface(), error,
                std::function(
                    [=]()
                    {
                        return o->responderVerificationStatus();
                    }
                ));
    }
    catch (const std::exception&)
    {
        o->get_bus().set_current_exception(std::current_exception());
        return 1;
    }
}

auto IdentityAuthentication::responderVerificationStatus(VerificationStatus value,
                                         bool skipSignal) ->
        VerificationStatus
{
    if (_responderVerificationStatus != value)
    {
        _responderVerificationStatus = value;
        if (!skipSignal)
        {
            _xyz_openbmc_project_attestation_IdentityAuthentication_interface.property_changed("ResponderVerificationStatus");
        }
    }

    return _responderVerificationStatus;
}

auto IdentityAuthentication::responderVerificationStatus(VerificationStatus val) ->
        VerificationStatus
{
    return responderVerificationStatus(val, false);
}

int IdentityAuthentication::_callback_set_ResponderVerificationStatus(
        sd_bus* /*bus*/, const char* /*path*/, const char* /*interface*/,
        const char* /*property*/, sd_bus_message* value, void* context,
        sd_bus_error* error)
{
    auto o = static_cast<IdentityAuthentication*>(context);

    try
    {
        return sdbusplus::sdbuspp::property_callback(
                value, o->get_bus().getInterface(), error,
                std::function(
                    [=](VerificationStatus&& arg)
                    {
                        o->responderVerificationStatus(std::move(arg));
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
namespace IdentityAuthentication
{
static const auto _property_ResponderVerificationStatus =
    utility::tuple_to_array(message::types::type_id<
            sdbusplus::common::xyz::openbmc_project::attestation::IdentityAuthentication::VerificationStatus>());
}
}


void IdentityAuthentication::setPropertyByName(const std::string& _name,
                                     const PropertiesVariant& val,
                                     bool skipSignal)
{
    if (_name == "ResponderVerificationStatus")
    {
        auto& v = std::get<VerificationStatus>(val);
        responderVerificationStatus(v, skipSignal);
        return;
    }
}

auto IdentityAuthentication::getPropertyByName(const std::string& _name) ->
        PropertiesVariant
{
    if (_name == "ResponderVerificationStatus")
    {
        return responderVerificationStatus();
    }

    return PropertiesVariant();
}



const vtable_t IdentityAuthentication::_vtable[] = {
    vtable::start(),

    vtable::property("ResponderVerificationStatus",
                     details::IdentityAuthentication::_property_ResponderVerificationStatus.data(),
                     _callback_get_ResponderVerificationStatus,
                     _callback_set_ResponderVerificationStatus,
                     vtable::property_::emits_change),

    vtable::end()
};

} // namespace sdbusplus::server::xyz::openbmc_project::attestation

