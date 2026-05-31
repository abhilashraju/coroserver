#include <sdbusplus/sdbus.hpp>
#include <sdbusplus/sdbuspp_support/server.hpp>
#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Attestation/SecureExchange/server.hpp>

#include <exception>
#include <string>
#include <tuple>

namespace sdbusplus::server::xyz::openbmc_project::attestation
{

int SecureExchange::_callback_ExchangeAppData(
    sd_bus_message* msg, void* context, sd_bus_error* error)
{
    auto o = static_cast<SecureExchange*>(context);

    try
    {
        return sdbusplus::sdbuspp::method_callback(
            msg, o->get_bus().getInterface(), error,
            std::function([=]() { return o->exchangeAppData(); }));
    }
    catch (const std::exception&)
    {
        o->get_bus().set_current_exception(std::current_exception());
        return 1;
    }
}

namespace details
{
namespace SecureExchange
{
static const auto _param_ExchangeAppData =
    utility::tuple_to_array(message::types::type_id<>());
static const auto _return_ExchangeAppData =
    utility::tuple_to_array(message::types::type_id<>());
} // namespace SecureExchange
} // namespace details

const vtable_t SecureExchange::_vtable[] = {
    vtable::start(),

    vtable::method("ExchangeAppData",
                   details::SecureExchange::_param_ExchangeAppData.data(),
                   details::SecureExchange::_return_ExchangeAppData.data(),
                   _callback_ExchangeAppData),

    vtable::end()};

} // namespace sdbusplus::server::xyz::openbmc_project::attestation
