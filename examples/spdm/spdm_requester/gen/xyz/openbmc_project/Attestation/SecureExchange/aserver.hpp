#pragma once
#include <sdbusplus/async/server.hpp>
#include <sdbusplus/server/interface.hpp>
#include <sdbusplus/server/transaction.hpp>
#include <xyz/openbmc_project/Attestation/SecureExchange/common.hpp>

#include <type_traits>

namespace sdbusplus::aserver::xyz::openbmc_project::attestation
{

namespace details
{
// forward declaration
template <typename Instance, typename Server>
class SecureExchange;
} // namespace details

template <typename Instance, typename Server = void>
struct SecureExchange :
    public std::conditional_t<
        std::is_void_v<Server>,
        sdbusplus::async::server_t<Instance, details::SecureExchange>,
        details::SecureExchange<Instance, Server>>
{
    template <typename... Args>
    SecureExchange(Args&&... args) :
        std::conditional_t<
            std::is_void_v<Server>,
            sdbusplus::async::server_t<Instance, details::SecureExchange>,
            details::SecureExchange<Instance, Server>>(
            std::forward<Args>(args)...)
    {}
};

namespace details
{

namespace server_details = sdbusplus::async::server::details;

template <typename Instance, typename Server>
class SecureExchange :
    public sdbusplus::common::xyz::openbmc_project::attestation::SecureExchange,
    protected server_details::server_context_friend
{
  public:
    explicit SecureExchange(const char* path) :
        _xyz_openbmc_project_attestation_SecureExchange_interface(
            _context(), path, interface, _vtable, this)
    {}

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

    /* Method tags. */
    struct exchange_app_data_t
    {
        using value_types = std::tuple<>;
        using return_type = void;
    };

  private:
    /** @return the async context */
    sdbusplus::async::context& _context()
    {
        return server_details::server_context_friend::context<
            Server, SecureExchange>(this);
    }

    sdbusplus::server::interface_t
        _xyz_openbmc_project_attestation_SecureExchange_interface;

    static constexpr auto _method_typeid_p_exchange_app_data =
        utility::tuple_to_array(message::types::type_id<>());

    static constexpr auto _method_typeid_r_exchange_app_data =
        utility::tuple_to_array(message::types::type_id<>());

    static int _callback_m_exchange_app_data(sd_bus_message* msg, void* context,
                                             sd_bus_error* error
                                             [[maybe_unused]])
        requires(server_details::has_method<exchange_app_data_t, Instance>)
    {
        auto self = static_cast<SecureExchange*>(context);
        auto self_i = static_cast<Instance*>(self);

        try
        {
            auto m = sdbusplus::message_t{msg};

            constexpr auto has_method_msg =
                server_details::has_method_msg<exchange_app_data_t, Instance>;

            if constexpr (has_method_msg)
            {
                constexpr auto is_async = std::is_same_v<
                    sdbusplus::async::task<void>,
                    decltype(self_i->method_call(exchange_app_data_t{}, m))>;

                if constexpr (!is_async)
                {
                    self_i->method_call(exchange_app_data_t{}, m);
                    auto r = m.new_method_return();
                    r.method_return();
                }
                else
                {
                    auto fn =
                        [](auto self, auto self_i,
                           sdbusplus::message_t m) -> sdbusplus::async::task<> {
                        try
                        {
                            co_await self_i->method_call(exchange_app_data_t{},
                                                         m);

                            auto r = m.new_method_return();
                            r.method_return();
                            co_return;
                        }
                        catch (const std::exception&)
                        {
                            self->_context().get_bus().set_current_exception(
                                std::current_exception());
                            co_return;
                        }
                    };

                    self->_context().spawn(std::move(fn(self, self_i, m)));
                }
            }
            else
            {
                constexpr auto is_async [[maybe_unused]] = std::is_same_v<
                    sdbusplus::async::task<void>,
                    decltype(self_i->method_call(exchange_app_data_t{}))>;

                if constexpr (!is_async)
                {
                    self_i->method_call(exchange_app_data_t{});
                    auto r = m.new_method_return();
                    r.method_return();
                }
                else
                {
                    auto fn =
                        [](auto self, auto self_i,
                           sdbusplus::message_t m) -> sdbusplus::async::task<> {
                        try
                        {
                            co_await self_i->method_call(exchange_app_data_t{});

                            auto r = m.new_method_return();
                            r.method_return();
                            co_return;
                        }
                        catch (const std::exception&)
                        {
                            self->_context().get_bus().set_current_exception(
                                std::current_exception());
                            co_return;
                        }
                    };

                    self->_context().spawn(std::move(fn(self, self_i, m)));
                }
            }
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

        vtable::method("ExchangeAppData",
                       _method_typeid_p_exchange_app_data.data(),
                       _method_typeid_r_exchange_app_data.data(),
                       _callback_m_exchange_app_data),

        vtable::end(),
    };
};

} // namespace details
} // namespace sdbusplus::aserver::xyz::openbmc_project::attestation
