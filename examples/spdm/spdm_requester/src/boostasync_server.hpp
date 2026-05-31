#pragma once
#include <sdbusplus/async/server.hpp>
#include <sdbusplus/server/interface.hpp>
#include <sdbusplus/server/transaction.hpp>
#include <xyz/openbmc_project/Attestation/MeasurementSet/common.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <type_traits>

namespace sdbusplus::aserver::xyz::openbmc_project::attestation
{

namespace details
{
// forward declaration
template <typename Instance, typename Server>
class MeasurementSet;
} // namespace details

template <typename Instance, typename Server = void>
struct MeasurementSet :
    public std::conditional_t<
        std::is_void_v<Server>,
        sdbusplus::async::server_t<Instance, details::MeasurementSet>,
        details::MeasurementSet<Instance, Server>>
{
    template <typename... Args>
    MeasurementSet(Args&&... args) :
        std::conditional_t<
            std::is_void_v<Server>,
            sdbusplus::async::server_t<Instance, details::MeasurementSet>,
            details::MeasurementSet<Instance, Server>>(
            std::forward<Args>(args)...)
    {}
};

namespace details
{

namespace server_details = sdbusplus::async::server::details;

template <typename Instance, typename Server>
class MeasurementSet :
    public sdbusplus::common::xyz::openbmc_project::attestation::MeasurementSet,
    protected server_details::server_context_friend
{
  public:
    explicit MeasurementSet(const char* path) :
        _xyz_openbmc_project_attestation_MeasurementSet_interface(
            _context(), path, interface, _vtable, this)
    {}

    /** @brief Emit interface added */
    void emit_added()
    {
        _xyz_openbmc_project_attestation_MeasurementSet_interface.emit_added();
    }

    /** @brief Emit interface removed */
    void emit_removed()
    {
        _xyz_openbmc_project_attestation_MeasurementSet_interface
            .emit_removed();
    }

    /* Property access tags. */

    /* Method tags. */
    struct spdm_get_signed_measurements_t
    {
        using value_types =
            std::tuple<std::vector<size_t>, std::string, size_t>;
        using return_type =
            std::tuple<sdbusplus::message::object_path, std::string,
                       std::string, std::string, std::string, std::string>;
    };

  protected:
  private:
    /** @return the async context */
    boost::asio::io_context& _context()
    {
        return ctx;
    }

    sdbusplus::server::interface_t
        _xyz_openbmc_project_attestation_MeasurementSet_interface;

    static constexpr auto _method_typeid_p_spdm_get_signed_measurements =
        utility::tuple_to_array(message::types::type_id<std::vector<size_t>,
                                                        std::string, size_t>());

    static constexpr auto _method_typeid_r_spdm_get_signed_measurements =
        utility::tuple_to_array(
            message::types::type_id<sdbusplus::message::object_path,
                                    std::string, std::string, std::string,
                                    std::string, std::string>());

    static int _callback_m_spdm_get_signed_measurements(
        sd_bus_message* msg, void* context,
        sd_bus_error* error [[maybe_unused]])
        requires(server_details::has_method<spdm_get_signed_measurements_t,
                                            Instance, std::vector<size_t>,
                                            std::string, size_t>)
    {
        auto self = static_cast<MeasurementSet*>(context);
        auto self_i = static_cast<Instance*>(self);

        try
        {
            auto m = sdbusplus::message_t{msg};
            auto [measurementIndices, nonce, slotId] =
                m.unpack<std::vector<size_t>, std::string, size_t>();

            constexpr auto has_method_msg = server_details::has_method_msg<
                spdm_get_signed_measurements_t, Instance, std::vector<size_t>,
                std::string, size_t>;

            if constexpr (has_method_msg)
            {
                constexpr auto is_async = std::is_same_v<
                    sdbusplus::async::task<std::tuple<
                        sdbusplus::message::object_path, std::string,
                        std::string, std::string, std::string, std::string>>,
                    decltype(self_i->method_call(
                        spdm_get_signed_measurements_t{}, m,
                        std::move(measurementIndices), std::move(nonce),
                        std::move(slotId)))>;

                if constexpr (!is_async)
                {
                    auto r = m.new_method_return();
                    std::apply(
                        [&](auto&&... v) { (r.append(std::move(v)), ...); },
                        self_i->method_call(spdm_get_signed_measurements_t{}, m,
                                            std::move(measurementIndices),
                                            std::move(nonce),
                                            std::move(slotId)));
                    r.method_return();
                }
                else
                {
                    auto fn = [self, self_i, m, measurementIndices, nonce,
                               slotId]() -> boost::asio::awaitable<void> {
                        try
                        {
                            auto r = m.new_method_return();
                            std::apply(
                                [&](auto&&... v) {
                                    (r.append(std::move(v)), ...);
                                },
                                co_await self_i->method_call(
                                    spdm_get_signed_measurements_t{}, m,
                                    std::move(measurementIndices),
                                    std::move(nonce), std::move(slotId)));

                            r.method_return();
                            co_return;
                        }
                        catch (const sdbusplus::xyz::openbmc_project::Common::
                                   Error::InvalidArgument& e)
                        {
                            m.new_method_error(e).method_return();
                            co_return;
                        }
                        catch (const sdbusplus::xyz::openbmc_project::Common::
                                   Error::InternalFailure& e)
                        {
                            m.new_method_error(e).method_return();
                            co_return;
                        }
                        catch (const std::exception&)
                        {
                            self->_context().get_bus().set_current_exception(
                                std::current_exception());
                            co_return;
                        }
                    };
                    boost::asio::co_spawn(self->_context(), std::move(fn),
                                          boost::asio::detached);
                }
            }
            else
            {
                constexpr auto is_async [[maybe_unused]] = std::is_same_v<
                    sdbusplus::async::task<std::tuple<
                        sdbusplus::message::object_path, std::string,
                        std::string, std::string, std::string, std::string>>,
                    decltype(self_i->method_call(
                        spdm_get_signed_measurements_t{},
                        std::move(measurementIndices), std::move(nonce),
                        std::move(slotId)))>;

                if constexpr (!is_async)
                {
                    auto r = m.new_method_return();
                    std::apply(
                        [&](auto&&... v) { (r.append(std::move(v)), ...); },
                        self_i->method_call(spdm_get_signed_measurements_t{},
                                            std::move(measurementIndices),
                                            std::move(nonce),
                                            std::move(slotId)));
                    r.method_return();
                }
                else
                {
                    auto fn = [](auto self, auto self_i, sdbusplus::message_t m,
                                 std::vector<size_t> measurementIndices,
                                 std::string nonce,
                                 size_t slotId) -> sdbusplus::async::task<> {
                        try
                        {
                            auto r = m.new_method_return();
                            std::apply(
                                [&](auto&&... v) {
                                    (r.append(std::move(v)), ...);
                                },
                                co_await self_i->method_call(
                                    spdm_get_signed_measurements_t{},
                                    std::move(measurementIndices),
                                    std::move(nonce), std::move(slotId)));

                            r.method_return();
                            co_return;
                        }
                        catch (const sdbusplus::xyz::openbmc_project::Common::
                                   Error::InvalidArgument& e)
                        {
                            m.new_method_error(e).method_return();
                            co_return;
                        }
                        catch (const sdbusplus::xyz::openbmc_project::Common::
                                   Error::InternalFailure& e)
                        {
                            m.new_method_error(e).method_return();
                            co_return;
                        }
                        catch (const std::exception&)
                        {
                            self->_context().get_bus().set_current_exception(
                                std::current_exception());
                            co_return;
                        }
                    };

                    self->_context().spawn(std::move(
                        fn(self, self_i, m, std::move(measurementIndices),
                           std::move(nonce), std::move(slotId))));
                }
            }
        }
        catch (const sdbusplus::xyz::openbmc_project::Common::Error::
                   InvalidArgument& e)
        {
            return sd_bus_error_set(error, e.name(), e.description());
        }
        catch (const sdbusplus::xyz::openbmc_project::Common::Error::
                   InternalFailure& e)
        {
            return sd_bus_error_set(error, e.name(), e.description());
        }
        catch (const std::exception&)
        {
            self->_context().get_bus().set_current_exception(
                std::current_exception());
            return -EINVAL;
        }

        return 1;
    }

    static constexpr sdbusplus::vtable_t _vtable[] = {
        vtable::start(),

        vtable::method("SPDMGetSignedMeasurements",
                       _method_typeid_p_spdm_get_signed_measurements.data(),
                       _method_typeid_r_spdm_get_signed_measurements.data(),
                       _callback_m_spdm_get_signed_measurements),

        vtable::end(),
    };
};

} // namespace details
} // namespace sdbusplus::aserver::xyz::openbmc_project::attestation
