#pragma once
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <sdbusplus/asio/aserver.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/interface.hpp>
#include <sdbusplus/server/transaction.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <type_traits>

#include <xyz/openbmc_project/Attestation/IdentityAuthentication/common.hpp>

namespace sdbusplus::asio::aserver::xyz::openbmc_project::attestation
{

namespace details
{
// forward declaration
template <typename Instance, typename Server>
class IdentityAuthentication;
} // namespace details

template <typename Instance, typename Server = void>
struct IdentityAuthentication :
    public std::conditional_t<
        std::is_void_v<Server>,
        sdbusplus::asio::async::server_t<Instance, details::IdentityAuthentication>,
        details::IdentityAuthentication<Instance, Server>>
{
    template <typename... Args>
    IdentityAuthentication(Args&&... args) :
        std::conditional_t<
            std::is_void_v<Server>,
            sdbusplus::asio::async::server_t<Instance, details::IdentityAuthentication>,
            details::IdentityAuthentication<Instance, Server>>(
            std::forward<Args>(args)...)
    {}
};

namespace details
{

namespace server_details = sdbusplus::asio::async::server::details;

template <typename Instance, typename Server>
class IdentityAuthentication :
    public sdbusplus::common::xyz::openbmc_project::attestation::IdentityAuthentication,
    protected server_details::server_context_friend
{
  public:
    explicit IdentityAuthentication(sdbusplus::bus::bus& bus, const char* path) :
        _xyz_openbmc_project_attestation_IdentityAuthentication_interface(
            bus, path, interface, _vtable, this)
    {}


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

    /* Property access tags. */
    struct responder_verification_status_t
    {
        using value_type = VerificationStatus;
        responder_verification_status_t() = default;
        explicit responder_verification_status_t(value_type) {}
    };

    /* Method tags. */

    auto responder_verification_status() const
        requires server_details::has_get_property_nomsg<responder_verification_status_t, Instance>
    {
        return static_cast<const Instance*>(this)->get_property(responder_verification_status_t{});
    }
    auto responder_verification_status(sdbusplus::message_t& m) const
        requires server_details::has_get_property_msg<responder_verification_status_t, Instance>
    {
        return static_cast<const Instance*>(this)->get_property(responder_verification_status_t{}, m);
    }
    auto responder_verification_status() const noexcept
        requires (!server_details::has_get_property<responder_verification_status_t, Instance>)
    {
        static_assert(
            !server_details::has_get_property_missing_const<responder_verification_status_t,
                                                            Instance>,
            "Missing const on get_property(responder_verification_status_t)?");
        return responder_verification_status_;
    }

    template <bool EmitSignal = true, typename Arg = VerificationStatus>
    void responder_verification_status(Arg&& new_value)
        requires server_details::has_set_property_nomsg<responder_verification_status_t, Instance,
                                                        VerificationStatus>
    {
        bool changed = static_cast<Instance*>(this)->set_property(
            responder_verification_status_t{}, std::forward<Arg>(new_value));

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_IdentityAuthentication_interface.property_changed("ResponderVerificationStatus");
        }
    }

    template <bool EmitSignal = true, typename Arg = VerificationStatus>
    void responder_verification_status(sdbusplus::message_t& m, Arg&& new_value)
        requires server_details::has_set_property_msg<responder_verification_status_t, Instance,
                                                      VerificationStatus>
    {
        bool changed = static_cast<Instance*>(this)->set_property(
            responder_verification_status_t{}, m, std::forward<Arg>(new_value));

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_IdentityAuthentication_interface.property_changed("ResponderVerificationStatus");
        }
    }

    template <bool EmitSignal = true, typename Arg = VerificationStatus>
    void responder_verification_status(Arg&& new_value)
        requires (!server_details::has_set_property<responder_verification_status_t, Instance,
                                                    VerificationStatus>)
    {
        static_assert(
            !server_details::has_get_property<responder_verification_status_t, Instance>,
            "Cannot create default set-property for 'responder_verification_status_t' with get-property overload.");

        bool changed = (new_value != responder_verification_status_);
        responder_verification_status_ = std::forward<Arg>(new_value);

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_IdentityAuthentication_interface.property_changed("ResponderVerificationStatus");
        }
    }


  protected:
    VerificationStatus responder_verification_status_ = VerificationStatus::Unknown;

  private:
    /** Wrapper to provide get_bus() compatibility */
    struct context_wrapper
    {
        boost::asio::io_context& ctx;
        
        struct bus_wrapper
        {
            void set_current_exception(std::exception_ptr) {}
        };
        
        bus_wrapper get_bus() { return bus_wrapper{}; }
    };

    /** @return the async context */
    context_wrapper _context()
    {
        return context_wrapper{server_details::server_context_friend::context<
            Server, IdentityAuthentication>(this)};
    }

    sdbusplus::server::interface_t
        _xyz_openbmc_project_attestation_IdentityAuthentication_interface;

    static constexpr auto _property_typeid_responder_verification_status =
        utility::tuple_to_array(message::types::type_id<VerificationStatus>());

    static int _callback_get_responder_verification_status(
        sd_bus*, const char*, const char*, const char*,
        sd_bus_message* reply, void* context,
        sd_bus_error* error [[maybe_unused]])
    {
        auto self = static_cast<IdentityAuthentication*>(context);

        try
        {
            auto m = sdbusplus::message_t{reply};

            // Set up the transaction.
            server::transaction::set_id(m);

            // Get property value and add to message.
            if constexpr (server_details::has_get_property_msg<responder_verification_status_t,
                                                               Instance>)
            {
                auto v = self->responder_verification_status(m);
                static_assert(std::is_convertible_v<decltype(v), VerificationStatus>,
                              "Property doesn't convert to 'VerificationStatus'.");
                m.append<VerificationStatus>(std::move(v));
            }
            else
            {
                auto v = self->responder_verification_status();
                static_assert(std::is_convertible_v<decltype(v), VerificationStatus>,
                              "Property doesn't convert to 'VerificationStatus'.");
                m.append<VerificationStatus>(std::move(v));
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

    static int _callback_set_responder_verification_status(
        sd_bus*, const char*, const char*, const char*,
        sd_bus_message* value[[maybe_unused]], void* context [[maybe_unused]],
        sd_bus_error* error [[maybe_unused]])
    {
        auto self = static_cast<IdentityAuthentication*>(context);

        try
        {
            auto m = sdbusplus::message_t{value};

            // Set up the transaction.
            server::transaction::set_id(m);

            auto new_value = m.unpack<VerificationStatus>();

            // Get property value and add to message.
            if constexpr (server_details::has_set_property_msg<
                              responder_verification_status_t, Instance, VerificationStatus>)
            {
                self->responder_verification_status(m, std::move(new_value));
            }
            else
            {
                self->responder_verification_status(std::move(new_value));
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

        vtable::property("ResponderVerificationStatus",
                         _property_typeid_responder_verification_status.data(),
                         _callback_get_responder_verification_status,
                         _callback_set_responder_verification_status,
                         vtable::property_::emits_change),

        vtable::end(),
    };
};

} // namespace details
} // namespace sdbusplus::asio::aserver::xyz::openbmc_project::attestation
