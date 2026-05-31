#pragma once
#include <sdbusplus/async/server.hpp>
#include <sdbusplus/server/interface.hpp>
#include <sdbusplus/server/transaction.hpp>

#include <type_traits>

#include <xyz/openbmc_project/Attestation/ComponentIntegrity/common.hpp>

namespace sdbusplus::aserver::xyz::openbmc_project::attestation
{

namespace details
{
// forward declaration
template <typename Instance, typename Server>
class ComponentIntegrity;
} // namespace details

template <typename Instance, typename Server = void>
struct ComponentIntegrity :
    public std::conditional_t<
        std::is_void_v<Server>,
        sdbusplus::async::server_t<Instance, details::ComponentIntegrity>,
        details::ComponentIntegrity<Instance, Server>>
{
    template <typename... Args>
    ComponentIntegrity(Args&&... args) :
        std::conditional_t<
            std::is_void_v<Server>,
            sdbusplus::async::server_t<Instance, details::ComponentIntegrity>,
            details::ComponentIntegrity<Instance, Server>>(std::forward<Args>(args)...)
    {}
};

namespace details
{

namespace server_details = sdbusplus::async::server::details;

template <typename Instance, typename Server>
class ComponentIntegrity :
    public sdbusplus::common::xyz::openbmc_project::attestation::ComponentIntegrity,
    protected server_details::server_context_friend
{
  public:
    explicit ComponentIntegrity(const char* path) :
        _xyz_openbmc_project_attestation_ComponentIntegrity_interface(
            _context(), path, interface, _vtable, this)
    {}


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

    /* Property access tags. */
    struct enabled_t
    {
        using value_type = bool;
        enabled_t() = default;
        explicit enabled_t(value_type) {}
    };
    struct type_t
    {
        using value_type = SecurityTechnologyType;
        type_t() = default;
        explicit type_t(value_type) {}
    };
    struct type_version_t
    {
        using value_type = std::string;
        type_version_t() = default;
        explicit type_version_t(value_type) {}
    };
    struct last_updated_t
    {
        using value_type = uint64_t;
        last_updated_t() = default;
        explicit last_updated_t(value_type) {}
    };

    /* Method tags. */
    struct set_provisioned_t
    {
        using value_types = std::tuple<bool>;
        using return_type = bool;
    };

    auto enabled() const
        requires server_details::has_get_property_nomsg<enabled_t, Instance>
    {
        return static_cast<const Instance*>(this)->get_property(enabled_t{});
    }
    auto enabled(sdbusplus::message_t& m) const
        requires server_details::has_get_property_msg<enabled_t, Instance>
    {
        return static_cast<const Instance*>(this)->get_property(enabled_t{}, m);
    }
    auto enabled() const noexcept
        requires (!server_details::has_get_property<enabled_t, Instance>)
    {
        static_assert(
            !server_details::has_get_property_missing_const<enabled_t,
                                                            Instance>,
            "Missing const on get_property(enabled_t)?");
        return enabled_;
    }

    auto type() const
        requires server_details::has_get_property_nomsg<type_t, Instance>
    {
        return static_cast<const Instance*>(this)->get_property(type_t{});
    }
    auto type(sdbusplus::message_t& m) const
        requires server_details::has_get_property_msg<type_t, Instance>
    {
        return static_cast<const Instance*>(this)->get_property(type_t{}, m);
    }
    auto type() const noexcept
        requires (!server_details::has_get_property<type_t, Instance>)
    {
        static_assert(
            !server_details::has_get_property_missing_const<type_t,
                                                            Instance>,
            "Missing const on get_property(type_t)?");
        return type_;
    }

    auto type_version() const
        requires server_details::has_get_property_nomsg<type_version_t, Instance>
    {
        return static_cast<const Instance*>(this)->get_property(type_version_t{});
    }
    auto type_version(sdbusplus::message_t& m) const
        requires server_details::has_get_property_msg<type_version_t, Instance>
    {
        return static_cast<const Instance*>(this)->get_property(type_version_t{}, m);
    }
    auto type_version() const noexcept
        requires (!server_details::has_get_property<type_version_t, Instance>)
    {
        static_assert(
            !server_details::has_get_property_missing_const<type_version_t,
                                                            Instance>,
            "Missing const on get_property(type_version_t)?");
        return type_version_;
    }

    auto last_updated() const
        requires server_details::has_get_property_nomsg<last_updated_t, Instance>
    {
        return static_cast<const Instance*>(this)->get_property(last_updated_t{});
    }
    auto last_updated(sdbusplus::message_t& m) const
        requires server_details::has_get_property_msg<last_updated_t, Instance>
    {
        return static_cast<const Instance*>(this)->get_property(last_updated_t{}, m);
    }
    auto last_updated() const noexcept
        requires (!server_details::has_get_property<last_updated_t, Instance>)
    {
        static_assert(
            !server_details::has_get_property_missing_const<last_updated_t,
                                                            Instance>,
            "Missing const on get_property(last_updated_t)?");
        return last_updated_;
    }

    template <bool EmitSignal = true, typename Arg = bool>
    void enabled(Arg&& new_value)
        requires server_details::has_set_property_nomsg<enabled_t, Instance,
                                                        bool>
    {
        bool changed = static_cast<Instance*>(this)->set_property(
            enabled_t{}, std::forward<Arg>(new_value));

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("Enabled");
        }
    }

    template <bool EmitSignal = true, typename Arg = bool>
    void enabled(sdbusplus::message_t& m, Arg&& new_value)
        requires server_details::has_set_property_msg<enabled_t, Instance,
                                                      bool>
    {
        bool changed = static_cast<Instance*>(this)->set_property(
            enabled_t{}, m, std::forward<Arg>(new_value));

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("Enabled");
        }
    }

    template <bool EmitSignal = true, typename Arg = bool>
    void enabled(Arg&& new_value)
        requires (!server_details::has_set_property<enabled_t, Instance,
                                                    bool>)
    {
        static_assert(
            !server_details::has_get_property<enabled_t, Instance>,
            "Cannot create default set-property for 'enabled_t' with get-property overload.");

        bool changed = (new_value != enabled_);
        enabled_ = std::forward<Arg>(new_value);

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("Enabled");
        }
    }

    template <bool EmitSignal = true, typename Arg = SecurityTechnologyType>
    void type(Arg&& new_value)
        requires server_details::has_set_property_nomsg<type_t, Instance,
                                                        SecurityTechnologyType>
    {
        bool changed = static_cast<Instance*>(this)->set_property(
            type_t{}, std::forward<Arg>(new_value));

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("Type");
        }
    }

    template <bool EmitSignal = true, typename Arg = SecurityTechnologyType>
    void type(sdbusplus::message_t& m, Arg&& new_value)
        requires server_details::has_set_property_msg<type_t, Instance,
                                                      SecurityTechnologyType>
    {
        bool changed = static_cast<Instance*>(this)->set_property(
            type_t{}, m, std::forward<Arg>(new_value));

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("Type");
        }
    }

    template <bool EmitSignal = true, typename Arg = SecurityTechnologyType>
    void type(Arg&& new_value)
        requires (!server_details::has_set_property<type_t, Instance,
                                                    SecurityTechnologyType>)
    {
        static_assert(
            !server_details::has_get_property<type_t, Instance>,
            "Cannot create default set-property for 'type_t' with get-property overload.");

        bool changed = (new_value != type_);
        type_ = std::forward<Arg>(new_value);

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("Type");
        }
    }

    template <bool EmitSignal = true, typename Arg = std::string>
    void type_version(Arg&& new_value)
        requires server_details::has_set_property_nomsg<type_version_t, Instance,
                                                        std::string>
    {
        bool changed = static_cast<Instance*>(this)->set_property(
            type_version_t{}, std::forward<Arg>(new_value));

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("TypeVersion");
        }
    }

    template <bool EmitSignal = true, typename Arg = std::string>
    void type_version(sdbusplus::message_t& m, Arg&& new_value)
        requires server_details::has_set_property_msg<type_version_t, Instance,
                                                      std::string>
    {
        bool changed = static_cast<Instance*>(this)->set_property(
            type_version_t{}, m, std::forward<Arg>(new_value));

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("TypeVersion");
        }
    }

    template <bool EmitSignal = true, typename Arg = std::string>
    void type_version(Arg&& new_value)
        requires (!server_details::has_set_property<type_version_t, Instance,
                                                    std::string>)
    {
        static_assert(
            !server_details::has_get_property<type_version_t, Instance>,
            "Cannot create default set-property for 'type_version_t' with get-property overload.");

        bool changed = (new_value != type_version_);
        type_version_ = std::forward<Arg>(new_value);

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("TypeVersion");
        }
    }

    template <bool EmitSignal = true, typename Arg = uint64_t>
    void last_updated(Arg&& new_value)
        requires server_details::has_set_property_nomsg<last_updated_t, Instance,
                                                        uint64_t>
    {
        bool changed = static_cast<Instance*>(this)->set_property(
            last_updated_t{}, std::forward<Arg>(new_value));

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("LastUpdated");
        }
    }

    template <bool EmitSignal = true, typename Arg = uint64_t>
    void last_updated(sdbusplus::message_t& m, Arg&& new_value)
        requires server_details::has_set_property_msg<last_updated_t, Instance,
                                                      uint64_t>
    {
        bool changed = static_cast<Instance*>(this)->set_property(
            last_updated_t{}, m, std::forward<Arg>(new_value));

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("LastUpdated");
        }
    }

    template <bool EmitSignal = true, typename Arg = uint64_t>
    void last_updated(Arg&& new_value)
        requires (!server_details::has_set_property<last_updated_t, Instance,
                                                    uint64_t>)
    {
        static_assert(
            !server_details::has_get_property<last_updated_t, Instance>,
            "Cannot create default set-property for 'last_updated_t' with get-property overload.");

        bool changed = (new_value != last_updated_);
        last_updated_ = std::forward<Arg>(new_value);

        if (changed && EmitSignal)
        {
            _xyz_openbmc_project_attestation_ComponentIntegrity_interface.property_changed("LastUpdated");
        }
    }


  protected:
    bool enabled_ = false;
    SecurityTechnologyType type_ = SecurityTechnologyType::Unknown;
    std::string type_version_;
    uint64_t last_updated_;

  private:
    /** @return the async context */
    sdbusplus::async::context& _context()
    {
        return server_details::server_context_friend::
            context<Server, ComponentIntegrity>(this);
    }

    sdbusplus::server::interface_t
        _xyz_openbmc_project_attestation_ComponentIntegrity_interface;

    static constexpr auto _property_typeid_enabled =
        utility::tuple_to_array(message::types::type_id<bool>());
    static constexpr auto _property_typeid_type =
        utility::tuple_to_array(message::types::type_id<SecurityTechnologyType>());
    static constexpr auto _property_typeid_type_version =
        utility::tuple_to_array(message::types::type_id<std::string>());
    static constexpr auto _property_typeid_last_updated =
        utility::tuple_to_array(message::types::type_id<uint64_t>());
    static constexpr auto _method_typeid_p_set_provisioned =
        utility::tuple_to_array(message::types::type_id<bool>());

    static constexpr auto _method_typeid_r_set_provisioned =
        utility::tuple_to_array(message::types::type_id<bool>());

    static int _callback_get_enabled(
        sd_bus*, const char*, const char*, const char*,
        sd_bus_message* reply, void* context,
        sd_bus_error* error [[maybe_unused]])
    {
        auto self = static_cast<ComponentIntegrity*>(context);

        try
        {
            auto m = sdbusplus::message_t{reply};

            // Set up the transaction.
            server::transaction::set_id(m);

            // Get property value and add to message.
            if constexpr (server_details::has_get_property_msg<enabled_t,
                                                               Instance>)
            {
                auto v = self->enabled(m);
                static_assert(std::is_convertible_v<decltype(v), bool>,
                              "Property doesn't convert to 'bool'.");
                m.append<bool>(std::move(v));
            }
            else
            {
                auto v = self->enabled();
                static_assert(std::is_convertible_v<decltype(v), bool>,
                              "Property doesn't convert to 'bool'.");
                m.append<bool>(std::move(v));
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

    static int _callback_set_enabled(
        sd_bus*, const char*, const char*, const char*,
        sd_bus_message* value[[maybe_unused]], void* context [[maybe_unused]],
        sd_bus_error* error [[maybe_unused]])
    {
        auto self = static_cast<ComponentIntegrity*>(context);

        try
        {
            auto m = sdbusplus::message_t{value};

            // Set up the transaction.
            server::transaction::set_id(m);

            auto new_value = m.unpack<bool>();

            // Get property value and add to message.
            if constexpr (server_details::has_set_property_msg<
                              enabled_t, Instance, bool>)
            {
                self->enabled(m, std::move(new_value));
            }
            else
            {
                self->enabled(std::move(new_value));
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

    static int _callback_get_type(
        sd_bus*, const char*, const char*, const char*,
        sd_bus_message* reply, void* context,
        sd_bus_error* error [[maybe_unused]])
    {
        auto self = static_cast<ComponentIntegrity*>(context);

        try
        {
            auto m = sdbusplus::message_t{reply};

            // Set up the transaction.
            server::transaction::set_id(m);

            // Get property value and add to message.
            if constexpr (server_details::has_get_property_msg<type_t,
                                                               Instance>)
            {
                auto v = self->type(m);
                static_assert(std::is_convertible_v<decltype(v), SecurityTechnologyType>,
                              "Property doesn't convert to 'SecurityTechnologyType'.");
                m.append<SecurityTechnologyType>(std::move(v));
            }
            else
            {
                auto v = self->type();
                static_assert(std::is_convertible_v<decltype(v), SecurityTechnologyType>,
                              "Property doesn't convert to 'SecurityTechnologyType'.");
                m.append<SecurityTechnologyType>(std::move(v));
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


    static int _callback_get_type_version(
        sd_bus*, const char*, const char*, const char*,
        sd_bus_message* reply, void* context,
        sd_bus_error* error [[maybe_unused]])
    {
        auto self = static_cast<ComponentIntegrity*>(context);

        try
        {
            auto m = sdbusplus::message_t{reply};

            // Set up the transaction.
            server::transaction::set_id(m);

            // Get property value and add to message.
            if constexpr (server_details::has_get_property_msg<type_version_t,
                                                               Instance>)
            {
                auto v = self->type_version(m);
                static_assert(std::is_convertible_v<decltype(v), std::string>,
                              "Property doesn't convert to 'std::string'.");
                m.append<std::string>(std::move(v));
            }
            else
            {
                auto v = self->type_version();
                static_assert(std::is_convertible_v<decltype(v), std::string>,
                              "Property doesn't convert to 'std::string'.");
                m.append<std::string>(std::move(v));
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


    static int _callback_get_last_updated(
        sd_bus*, const char*, const char*, const char*,
        sd_bus_message* reply, void* context,
        sd_bus_error* error [[maybe_unused]])
    {
        auto self = static_cast<ComponentIntegrity*>(context);

        try
        {
            auto m = sdbusplus::message_t{reply};

            // Set up the transaction.
            server::transaction::set_id(m);

            // Get property value and add to message.
            if constexpr (server_details::has_get_property_msg<last_updated_t,
                                                               Instance>)
            {
                auto v = self->last_updated(m);
                static_assert(std::is_convertible_v<decltype(v), uint64_t>,
                              "Property doesn't convert to 'uint64_t'.");
                m.append<uint64_t>(std::move(v));
            }
            else
            {
                auto v = self->last_updated();
                static_assert(std::is_convertible_v<decltype(v), uint64_t>,
                              "Property doesn't convert to 'uint64_t'.");
                m.append<uint64_t>(std::move(v));
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



    static int _callback_m_set_provisioned(sd_bus_message* msg, void* context,
                                     sd_bus_error* error [[maybe_unused]])
        requires (server_details::has_method<
                            set_provisioned_t, Instance, bool>)
    {
        auto self = static_cast<ComponentIntegrity*>(context);
        auto self_i = static_cast<Instance*>(self);

        try
        {
            auto m = sdbusplus::message_t{msg};
            auto provisioned = m.unpack<bool>();

            constexpr auto has_method_msg =
                server_details::has_method_msg<
                    set_provisioned_t, Instance, bool>;

            if constexpr (has_method_msg)
            {
                constexpr auto is_async = std::is_same_v<
                    sdbusplus::async::task<bool>,
                    decltype(self_i->method_call(set_provisioned_t{}, m,
                                std::move(provisioned)))>;

                if constexpr (!is_async)
                {
                    auto r = m.new_method_return();
                    r.append(self_i->method_call(set_provisioned_t{}, m,
                            std::move(provisioned)));
                    r.method_return();
                }
                else
                {
                    auto fn = [](auto self, auto self_i,
                                 sdbusplus::message_t m,
                                 bool provisioned)
                            -> sdbusplus::async::task<>
                    {
                        try
                        {

                            auto r = m.new_method_return();
                            r.append(co_await self_i->method_call(
                                set_provisioned_t{}, m, std::move(provisioned)));

                            r.method_return();
                            co_return;
                        }
                        catch(const std::exception&)
                        {
                            self->_context().get_bus().set_current_exception(
                                std::current_exception());
                            co_return;
                        }
                    };

                    self->_context().spawn(
                        std::move(fn(self, self_i, m, std::move(provisioned))));
                }
            }
            else
            {
                constexpr auto is_async [[maybe_unused]] = std::is_same_v<
                    sdbusplus::async::task<bool>,
                    decltype(self_i->method_call(set_provisioned_t{},
                                std::move(provisioned)))>;

                if constexpr (!is_async)
                {
                    auto r = m.new_method_return();
                    r.append(self_i->method_call(set_provisioned_t{},
                            std::move(provisioned)));
                    r.method_return();
                }
                else
                {
                    auto fn = [](auto self, auto self_i,
                                 sdbusplus::message_t m,
                                 bool provisioned)
                            -> sdbusplus::async::task<>
                    {
                        try
                        {

                            auto r = m.new_method_return();
                            r.append(co_await self_i->method_call(
                                set_provisioned_t{}, std::move(provisioned)));

                            r.method_return();
                            co_return;
                        }
                        catch(const std::exception&)
                        {
                            self->_context().get_bus().set_current_exception(
                                std::current_exception());
                            co_return;
                        }
                    };

                    self->_context().spawn(
                        std::move(fn(self, self_i, m, std::move(provisioned))));
                }
            }
        }
        catch(const std::exception&)
        {
            self->_context().get_bus().set_current_exception(
                std::current_exception());
            return -EINVAL;
        }

        return 1;
    }

    static constexpr sdbusplus::vtable_t _vtable[] = {
        vtable::start(),

        vtable::property("Enabled",
                         _property_typeid_enabled.data(),
                         _callback_get_enabled,
                         _callback_set_enabled,
                         vtable::property_::emits_change),
        vtable::property("Type",
                         _property_typeid_type.data(),
                         _callback_get_type,
                         vtable::property_::emits_change),
        vtable::property("TypeVersion",
                         _property_typeid_type_version.data(),
                         _callback_get_type_version,
                         vtable::property_::emits_change),
        vtable::property("LastUpdated",
                         _property_typeid_last_updated.data(),
                         _callback_get_last_updated,
                         vtable::property_::emits_change),
        vtable::method("SetProvisioned",
                       _method_typeid_p_set_provisioned.data(),
                       _method_typeid_r_set_provisioned.data(),
                       _callback_m_set_provisioned),

        vtable::end(),
    };
};

} // namespace details
} // namespace sdbusplus::aserver::xyz::openbmc_project::attestation

