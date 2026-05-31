#pragma once
#include <sdbusplus/async/client.hpp>
#include <sdbusplus/async/execution.hpp>
#include <xyz/openbmc_project/Attestation/SecureExchange/common.hpp>

#include <type_traits>
#include <variant>

namespace sdbusplus::client::xyz::openbmc_project::attestation
{

namespace details
{
// forward declaration
template <typename Client, typename Proxy>
class SecureExchange;
} // namespace details

/** Alias class so we can use the client in both a client_t aggregation
 *  and individually.
 *
 *  sdbusplus::async::client_t<SecureExchange>() or
 *  SecureExchange() both construct an equivalent instance.
 */
template <typename Client = void, typename Proxy = void>
struct SecureExchange :
    public std::conditional_t<
        std::is_void_v<Client>,
        sdbusplus::async::client_t<details::SecureExchange>,
        details::SecureExchange<Client, Proxy>>
{
    template <typename... Args>
    SecureExchange(Args&&... args) :
        std::conditional_t<std::is_void_v<Client>,
                           sdbusplus::async::client_t<details::SecureExchange>,
                           details::SecureExchange<Client, Proxy>>(
            std::forward<Args>(args)...)
    {}
};

namespace details
{

template <typename Client, typename Proxy>
class SecureExchange :
    public sdbusplus::common::xyz::openbmc_project::attestation::SecureExchange,
    private sdbusplus::async::client::details::client_context_friend
{
  public:
    friend Client;
    template <typename, typename>
    friend struct sdbusplus::client::xyz::openbmc_project::attestation::
        SecureExchange;

    // Delete default constructor as these should only be constructed
    // indirectly through sdbusplus::async::client_t.
    SecureExchange() = delete;

    /** @brief ExchangeAppData
     *  Perform application data exchange over the active SPDM secure session.
     *  The session must already be established and authenticated.
     */
    auto exchange_app_data()
    {
        return proxy.template call<>(context(), "ExchangeAppData");
    }

  private:
    // Conversion constructor from proxy used by client_t.
    explicit constexpr SecureExchange(Proxy p) : proxy(p.interface(interface))
    {}

    sdbusplus::async::context& context()
    {
        return sdbusplus::async::client::details::client_context_friend::
            context<Client, SecureExchange>(this);
    }

    decltype(std::declval<Proxy>().interface(interface)) proxy = {};
};

} // namespace details

} // namespace sdbusplus::client::xyz::openbmc_project::attestation
