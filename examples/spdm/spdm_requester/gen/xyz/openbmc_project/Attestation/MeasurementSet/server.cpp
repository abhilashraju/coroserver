#include <exception>
#include <map>
#include <sdbusplus/sdbus.hpp>
#include <sdbusplus/sdbuspp_support/server.hpp>
#include <sdbusplus/server.hpp>
#include <string>
#include <tuple>

#include <xyz/openbmc_project/Attestation/MeasurementSet/server.hpp>

namespace sdbusplus::server::xyz::openbmc_project::attestation
{

int MeasurementSet::_callback_SPDMGetSignedMeasurements(
        sd_bus_message* msg, void* context, sd_bus_error* error)
{
    auto o = static_cast<MeasurementSet*>(context);

    try
    {
        return sdbusplus::sdbuspp::method_callback<true>(
                msg, o->get_bus().getInterface(), error,
                std::function(
                    [=](std::vector<size_t>&& measurementIndices, std::string&& nonce, size_t&& slotId)
                    {
                        return o->spdmGetSignedMeasurements(
                                measurementIndices, nonce, slotId);
                    }
                ));
    }
    catch(const sdbusplus::xyz::openbmc_project::Common::Error::InvalidArgument& e)
    {
        return e.set_error(o->get_bus().getInterface(), error);
    }
    catch(const sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure& e)
    {
        return e.set_error(o->get_bus().getInterface(), error);
    }
    catch (const std::exception&)
    {
        o->get_bus().set_current_exception(std::current_exception());
        return 1;
    }
}

namespace details
{
namespace MeasurementSet
{
static const auto _param_SPDMGetSignedMeasurements =
        utility::tuple_to_array(message::types::type_id<
                std::vector<size_t>, std::string, size_t>());
static const auto _return_SPDMGetSignedMeasurements =
        utility::tuple_to_array(message::types::type_id<
                sdbusplus::message::object_path, std::string, std::string, std::string, std::string, std::string>());
}
}
int MeasurementSet::_callback_ExchangeCertificate(
        sd_bus_message* msg, void* context, sd_bus_error* error)
{
    auto o = static_cast<MeasurementSet*>(context);

    try
    {
        return sdbusplus::sdbuspp::method_callback<true>(
                msg, o->get_bus().getInterface(), error,
                std::function(
                    [=]()
                    {
                        return o->exchangeCertificate(
                                );
                    }
                ));
    }
    catch(const sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure& e)
    {
        return e.set_error(o->get_bus().getInterface(), error);
    }
    catch(const sdbusplus::xyz::openbmc_project::Common::Error::Timeout& e)
    {
        return e.set_error(o->get_bus().getInterface(), error);
    }
    catch (const std::exception&)
    {
        o->get_bus().set_current_exception(std::current_exception());
        return 1;
    }
}

namespace details
{
namespace MeasurementSet
{
static const auto _param_ExchangeCertificate =
        utility::tuple_to_array(message::types::type_id<
                >());
static const auto _return_ExchangeCertificate =
        utility::tuple_to_array(message::types::type_id<
                bool, std::string>());
}
}





const vtable_t MeasurementSet::_vtable[] = {
    vtable::start(),

    vtable::method("SPDMGetSignedMeasurements",
                   details::MeasurementSet::_param_SPDMGetSignedMeasurements.data(),
                   details::MeasurementSet::_return_SPDMGetSignedMeasurements.data(),
                   _callback_SPDMGetSignedMeasurements),

    vtable::method("ExchangeCertificate",
                   details::MeasurementSet::_param_ExchangeCertificate.data(),
                   details::MeasurementSet::_return_ExchangeCertificate.data(),
                   _callback_ExchangeCertificate),

    vtable::end()
};

} // namespace sdbusplus::server::xyz::openbmc_project::attestation

