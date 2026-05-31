#pragma once
#include <sdbusplus/async/client.hpp>
#include <sdbusplus/async/execution.hpp>
#include <type_traits>
#include <variant>

#include <xyz/openbmc_project/Attestation/ComponentIntegrity/common.hpp>

namespace sdbusplus::client::xyz::openbmc_project::attestation
{

namespace details
{
// forward declaration
template <typename Client, typename Proxy>
class ComponentIntegrity;
} // namespace details

/** Alias class so we can use the client in both a client_t aggregation
 *  and individually.
 *
 *  sdbusplus::async::client_t<ComponentIntegrity>() or
 *  ComponentIntegrity() both construct an equivalent instance.
 */
template <typename Client = void, typename Proxy = void>
struct ComponentIntegrity :
    public std::conditional_t<std::is_void_v<Client>,
                              sdbusplus::async::client_t<details::ComponentIntegrity>,
                              details::ComponentIntegrity<Client, Proxy>>
{
    template <typename... Args>
    ComponentIntegrity(Args&&... args) :
        std::conditional_t<std::is_void_v<Client>,
                           sdbusplus::async::client_t<details::ComponentIntegrity>,
                           details::ComponentIntegrity<Client, Proxy>>(
            std::forward<Args>(args)...)
    {}
};

namespace details
{

template <typename Client, typename Proxy>
class ComponentIntegrity :
    public sdbusplus::common::xyz::openbmc_project::attestation::ComponentIntegrity,
    private sdbusplus::async::client::details::client_context_friend
{
  public:
    friend Client;
    template <typename, typename>
    friend struct sdbusplus::client::xyz::openbmc_project::attestation::ComponentIntegrity;

    // Delete default constructor as these should only be constructed
    // indirectly through sdbusplus::async::client_t.
    ComponentIntegrity() = delete;

    /** @brief SetProvisioned
     *  Set the provisioned state of the component. This method allows updating the provisioning status of a component that supports security protocols.
     *
     *  @param[in] provisioned - The provisioned state to set. True indicates the component is provisioned, false indicates it is not provisioned.
     *
     *  @return success[bool] - Returns true if the provisioned state was successfully set, false otherwise.
     */
    auto set_provisioned(bool provisioned)
    {
        return proxy.template call<bool>(context(), "SetProvisioned", provisioned);
    }

    /** Get value of Enabled
     *  An indication of whether security protocols are enabled for the component.
     */
    auto enabled()
    {
        return proxy.template get_property<bool>(context(), "Enabled");
    }

    /** Set value of Enabled
     *  An indication of whether security protocols are enabled for the component.
     */
    auto enabled(auto value)
    {
        return proxy.template set_property<bool>(
            context(), "Enabled", std::forward<decltype(value)>(value));
    }

    /** Get value of Type
     *  The type of security technology for the component.
     */
    auto type()
    {
        return proxy.template get_property<SecurityTechnologyType>(context(), "Type");
    }


    /** Get value of TypeVersion
     *  The version of the security technology. Human readable format, e.g. "1.1" for SPDM.
     */
    auto type_version()
    {
        return proxy.template get_property<std::string>(context(), "TypeVersion");
    }


    /** Get value of LastUpdated
     *  The date and time when information for the component was last updated. Firmware update, device certificate change or other device state change that leads to component integrity change should update this date. It is represented in milliseconds since the UNIX epoch.
     */
    auto last_updated()
    {
        return proxy.template get_property<uint64_t>(context(), "LastUpdated");
    }



    auto properties()
    {
        return proxy.template get_all_properties<PropertiesVariant>(context()) |
               sdbusplus::async::execution::then([](auto&& v) {
                   properties_t result;
                   for (const auto& [property, value] : v)
                   {
                       std::visit(
                           [&](auto v) {
                               if (property == "Enabled")
                               {
                                   if constexpr (std::is_same_v<
                                                     std::decay_t<decltype(v)>,
                                                     bool>)
                                   {
                                       result.enabled = v;
                                       return;
                                   }
                                   else
                                   {
                                       throw exception::UnpackPropertyError(
                                           property,
                                           UnpackErrorReason::wrongType);
                                   }
                               }
                               if (property == "Type")
                               {
                                   if constexpr (std::is_same_v<
                                                     std::decay_t<decltype(v)>,
                                                     SecurityTechnologyType>)
                                   {
                                       result.type = v;
                                       return;
                                   }
                                   else
                                   {
                                       throw exception::UnpackPropertyError(
                                           property,
                                           UnpackErrorReason::wrongType);
                                   }
                               }
                               if (property == "TypeVersion")
                               {
                                   if constexpr (std::is_same_v<
                                                     std::decay_t<decltype(v)>,
                                                     std::string>)
                                   {
                                       result.type_version = v;
                                       return;
                                   }
                                   else
                                   {
                                       throw exception::UnpackPropertyError(
                                           property,
                                           UnpackErrorReason::wrongType);
                                   }
                               }
                               if (property == "LastUpdated")
                               {
                                   if constexpr (std::is_same_v<
                                                     std::decay_t<decltype(v)>,
                                                     uint64_t>)
                                   {
                                       result.last_updated = v;
                                       return;
                                   }
                                   else
                                   {
                                       throw exception::UnpackPropertyError(
                                           property,
                                           UnpackErrorReason::wrongType);
                                   }
                               }
                           },
                           value);
                   }
                   return result;
               });
    }

  private:
    // Conversion constructor from proxy used by client_t.
    explicit constexpr ComponentIntegrity(Proxy p) :
        proxy(p.interface(interface))
    {}

    sdbusplus::async::context& context()
    {
        return sdbusplus::async::client::details::client_context_friend::
            context<Client, ComponentIntegrity>(this);
    }

    decltype(std::declval<Proxy>().interface(interface)) proxy = {};
};

} // namespace details

} // namespace sdbusplus::client::xyz::openbmc_project::attestation

