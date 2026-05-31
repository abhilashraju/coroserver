#pragma once
#include <sdbusplus/async/client.hpp>
#include <sdbusplus/async/execution.hpp>
#include <type_traits>
#include <variant>

#include <xyz/openbmc_project/Attestation/IdentityAuthentication/common.hpp>

namespace sdbusplus::client::xyz::openbmc_project::attestation
{

namespace details
{
// forward declaration
template <typename Client, typename Proxy>
class IdentityAuthentication;
} // namespace details

/** Alias class so we can use the client in both a client_t aggregation
 *  and individually.
 *
 *  sdbusplus::async::client_t<IdentityAuthentication>() or
 *  IdentityAuthentication() both construct an equivalent instance.
 */
template <typename Client = void, typename Proxy = void>
struct IdentityAuthentication :
    public std::conditional_t<std::is_void_v<Client>,
                              sdbusplus::async::client_t<details::IdentityAuthentication>,
                              details::IdentityAuthentication<Client, Proxy>>
{
    template <typename... Args>
    IdentityAuthentication(Args&&... args) :
        std::conditional_t<std::is_void_v<Client>,
                           sdbusplus::async::client_t<details::IdentityAuthentication>,
                           details::IdentityAuthentication<Client, Proxy>>(
            std::forward<Args>(args)...)
    {}
};

namespace details
{

template <typename Client, typename Proxy>
class IdentityAuthentication :
    public sdbusplus::common::xyz::openbmc_project::attestation::IdentityAuthentication,
    private sdbusplus::async::client::details::client_context_friend
{
  public:
    friend Client;
    template <typename, typename>
    friend struct sdbusplus::client::xyz::openbmc_project::attestation::IdentityAuthentication;

    // Delete default constructor as these should only be constructed
    // indirectly through sdbusplus::async::client_t.
    IdentityAuthentication() = delete;

    /** Get value of ResponderVerificationStatus
     *  The status of the verification of the identity of the component.
     */
    auto responder_verification_status()
    {
        return proxy.template get_property<VerificationStatus>(context(), "ResponderVerificationStatus");
    }

    /** Set value of ResponderVerificationStatus
     *  The status of the verification of the identity of the component.
     */
    auto responder_verification_status(auto value)
    {
        return proxy.template set_property<VerificationStatus>(
            context(), "ResponderVerificationStatus", std::forward<decltype(value)>(value));
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
                               if (property == "ResponderVerificationStatus")
                               {
                                   if constexpr (std::is_same_v<
                                                     std::decay_t<decltype(v)>,
                                                     VerificationStatus>)
                                   {
                                       result.responder_verification_status = v;
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
    explicit constexpr IdentityAuthentication(Proxy p) :
        proxy(p.interface(interface))
    {}

    sdbusplus::async::context& context()
    {
        return sdbusplus::async::client::details::client_context_friend::
            context<Client, IdentityAuthentication>(this);
    }

    decltype(std::declval<Proxy>().interface(interface)) proxy = {};
};

} // namespace details

} // namespace sdbusplus::client::xyz::openbmc_project::attestation

