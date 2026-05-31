#pragma once
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <sdbusplus/asio/aserver.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/interface.hpp>
#include <sdbusplus/server/transaction.hpp>
#include <xyz/openbmc_project/Attestation/SecureExchange/common.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <type_traits>

namespace sdbusplus::asio::aserver::xyz::openbmc_project::attestation
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
        sdbusplus::asio::async::server_t<Instance, details::SecureExchange>,
        details::SecureExchange<Instance, Server>>
{
    template <typename... Args>
    SecureExchange(Args&&... args) :
        std::conditional_t<
            std::is_void_v<Server>,
            sdbusplus::asio::async::server_t<Instance, details::SecureExchange>,
            details::SecureExchange<Instance, Server>>(
            std::forward<Args>(args)...)
    {}
};

namespace details
{

namespace server_details = sdbusplus::asio::async::server::details;

template <typename Instance, typename Server>
class SecureExchange :
    public sdbusplus::common::xyz::openbmc_project::attestation::SecureExchange,
    protected server_details::server_context_friend
{
  public:
    explicit SecureExchange(sdbusplus::bus::bus& bus, const char* path) :
        _xyz_openbmc_project_attestation_SecureExchange_interface(
            bus, path, interface, _vtable, this)
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
    /** Wrapper to provide get_bus() compatibility */
    struct context_wrapper
    {
        boost::asio::io_context& ctx;

        struct bus_wrapper
        {
            void set_current_exception(std::exception_ptr) {}
        };

        bus_wrapper get_bus()
        {
            return bus_wrapper{};
        }
    };

    /** @return the async context */
    context_wrapper _context()
    {
        return context_wrapper{server_details::server_context_friend::context<
            Server, SecureExchange>(this)};
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
                    boost::asio::awaitable<void>,
                    decltype(self_i->method_call(exchange_app_data_t{}, m))>;

                if constexpr (!is_async)
                {
                    self_i->method_call(exchange_app_data_t{}, m);
                    auto r = m.new_method_return();
                    r.method_return();
                }
                else
                {
                    auto fn = [](auto self, auto self_i, sdbusplus::message_t m)
                        -> boost::asio::awaitable<void> {
                        try
                        {
                            co_await self_i->method_call(exchange_app_data_t{},
                                                         m);

                            auto r = m.new_method_return();
                            r.method_return();
                            co_return;
                        }
                        catch (const sdbusplus::xyz::openbmc_project::Common::
                                   Error::InternalFailure& e)
                        {
                            m.new_method_error(e).method_return();
                            co_return;
                        }
                        catch (const sdbusplus::xyz::openbmc_project::Common::
                                   Error::Timeout& e)
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

                    boost::asio::co_spawn(self->_context().ctx,
                                          fn(self, self_i, m),
                                          boost::asio::detached);
                }
            }
            else
            {
                constexpr auto is_async [[maybe_unused]] = std::is_same_v<
                    boost::asio::awaitable<void>,
                    decltype(self_i->method_call(exchange_app_data_t{}))>;

                if constexpr (!is_async)
                {
                    self_i->method_call(exchange_app_data_t{});
                    auto r = m.new_method_return();
                    r.method_return();
                }
                else
                {
                    auto fn = [](auto self, auto self_i, sdbusplus::message_t m)
                        -> boost::asio::awaitable<void> {
                        try
                        {
                            co_await self_i->method_call(exchange_app_data_t{});

                            auto r = m.new_method_return();
                            r.method_return();
                            co_return;
                        }
                        catch (const sdbusplus::xyz::openbmc_project::Common::
                                   Error::InternalFailure& e)
                        {
                            m.new_method_error(e).method_return();
                            co_return;
                        }
                        catch (const sdbusplus::xyz::openbmc_project::Common::
                                   Error::Timeout& e)
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

                    boost::asio::co_spawn(self->_context().ctx,
                                          fn(self, self_i, m),
                                          boost::asio::detached);
                }
            }
        }
        catch (const sdbusplus::xyz::openbmc_project::Common::Error::
                   InternalFailure& e)
        {
            return e.set_error(error);
        }
        catch (const sdbusplus::xyz::openbmc_project::Common::Error::Timeout& e)
        {
            return e.set_error(error);
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
} // namespace sdbusplus::asio::aserver::xyz::openbmc_project::attestation
