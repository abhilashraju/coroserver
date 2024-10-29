#pragma once
#include "beastdefs.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/asio/sd_event.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/timer.hpp>
inline net::use_awaitable_t<>& mut_awaitable()
{
    static net::use_awaitable_t<> myawitable;
    return myawitable;
}

template <typename... Types>
using PrependEC = std::tuple<boost::system::error_code, Types...>;
template <typename... RetTypes>
using ReturnTuple = std::conditional_t<
    std::is_same_v<boost::system::error_code,
                   std::tuple_element_t<0, std::tuple<RetTypes...>>>,
    std::tuple<RetTypes...>, PrependEC<RetTypes...>>;

template <typename... Types>
using AwaitableResult = net::awaitable<ReturnTuple<Types...>>;

template <typename... Ret, typename HanlderFunc>
auto make_awitable_handler(HanlderFunc&& h)
{
    return [h = std::move(h)]() -> AwaitableResult<Ret...> {
        co_return co_await net::async_initiate<
            net::use_awaitable_t<>, ReturnTuple<Ret...>(ReturnTuple<Ret...>)>(
            [h = std::move(h)](auto handler) {
                if constexpr (std::is_same_v<
                                  boost::system::error_code,
                                  std::tuple_element_t<0, std::tuple<Ret...>>>)
                {
                    auto callback =
                        [handler = std::move(handler)](Ret... values) mutable {
                            handler(ReturnTuple<Ret...>{std::move(values)...});
                        };
                    h(std::move(callback));
                }
                else
                {
                    auto callback = [handler = std::move(handler)](
                                        boost::system::error_code ec,
                                        Ret... values) mutable {
                        handler(ReturnTuple<Ret...>{ec, std::move(values)...});
                    };
                    h(std::move(callback));
                }
            },
            mut_awaitable());
    };
}

template <typename... RetTypes, typename... InputArgs>
inline auto awaitable_dbus_method_call(
    sdbusplus::asio::connection& conn, const std::string& service,
    const std::string& objpath, const std::string& interf,
    const std::string& method,
    const InputArgs&... a) -> AwaitableResult<RetTypes...>
{
    auto h = make_awitable_handler<RetTypes...>([&](auto handler) {
        conn.async_method_call(
            [handler = std::move(handler)](boost::system::error_code ec,
                                           RetTypes... values) mutable {
                handler(ec, std::move(values)...);
            },
            service, objpath, interf, method, a...);
    });
    co_return co_await h();
}

template <typename Type>
inline AwaitableResult<Type>
    getProperty(sdbusplus::asio::connection& conn, const std::string& service,
                const std::string& objpath, const std::string& interf,
                const std::string& property)
{
    auto [ec, value] =
        co_await awaitable_dbus_method_call<std::variant<std::monostate, Type>>(
            conn, service, objpath, "org.freedesktop.DBus.Properties", "Get",
            interf, property);

    co_return ReturnTuple<Type>{ec, std::get<Type>(value)};
}

template <typename InputArgs>
inline AwaitableResult<boost::system::error_code>
    setProperty(sdbusplus::asio::connection& conn, const std::string& service,
                const std::string& objpath, const std::string& interf,
                const std::string& property, const InputArgs& value)
{
    auto h =
        make_awitable_handler<boost::system::error_code>([&](auto handler) {
            sdbusplus::asio::setProperty(
                conn, service, objpath, interf, property, value,
                [handler = std::move(handler)](
                    boost::system::error_code ec) mutable { handler(ec); });
        });
    co_return co_await h();
}

template <typename VariantType>
inline AwaitableResult<std::vector<std::pair<std::string, VariantType>>>
    getAllProperties(sdbusplus::asio::connection& bus,
                     const std::string& service, const std::string& path,
                     const std::string& interface)
{
    using ReturnType = std::vector<std::pair<std::string, VariantType>>;
    auto h = make_awitable_handler<ReturnType>([&](auto handler) {
        bus.async_method_call(
            [handler = std::move(handler)](boost::system::error_code ec,
                                           const ReturnType& data) mutable {
                handler(ec, data);
            },
            service, path, "org.freedesktop.DBus.Properties", "GetAll",
            interface);
    });
    co_return co_await h();
}

template <typename SubTreeType>
inline AwaitableResult<SubTreeType>
    getSubTree(sdbusplus::asio::connection& bus, const std::string& path,
               int depth, const std::vector<std::string>& interfaces = {})
{
    auto h = make_awitable_handler<SubTreeType>([&](auto handler) {
        bus.async_method_call(
            [handler = std::move(handler)](boost::system::error_code ec,
                                           SubTreeType subtree) mutable {
                handler(ec, std::move(subtree));
            },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree", path, depth,
            interfaces);
    });
    co_return co_await h();
}

// template <typename... RetTypes, typename... InputArgs>
// inline auto awaitable_dbus_method_call(
//     sdbusplus::asio::connection& conn, const std::string& service,
//     const std::string& objpath, const std::string& interf,
//     const std::string& method,
//     const InputArgs&... a) -> AwaitableResult<RetTypes...>
// {
//       co_return co_await net::async_initiate<
//         net::use_awaitable_t<>,
//         ReturnTuple<RetTypes...>(ReturnTuple<RetTypes...>)>(
//         [&](auto handler) {
//             conn.async_method_call(
//                 [handler = std::move(handler)](boost::system::error_code ec,
//                                                RetTypes... values) mutable {
//                     handler(ReturnTuple<RetTypes...>{ec,
//                     std::move(values)...});
//                 },
//                 service, objpath, interf, method, a...);
//         },
//         mut_awaitable());
// }
// template <typename InputArgs>
// inline AwaitableResult<boost::system::error_code>
//     setProperty(sdbusplus::asio::connection& conn, const std::string&
//     service,
//                 const std::string& objpath, const std::string& interf,
//                 const std::string& property, const InputArgs& value)
// {
//     co_return co_await net::async_initiate<
//         net::use_awaitable_t<>, ReturnTuple<boost::system::error_code>(
//                                     ReturnTuple<boost::system::error_code>)>(
//         [&](auto handler) {
//             sdbusplus::asio::setProperty(
//                 conn, service, objpath, interf, property, value,
//                 [handler = std::move(handler)](
//                     boost::system::error_code ec) mutable {
//                     handler(ReturnTuple<boost::system::error_code>{ec});
//                 });
//         },
//         mut_awaitable());
// }
// template <typename VariantType>
// inline AwaitableResult<std::vector<std::pair<std::string, VariantType>>>
//     getAllProperties(sdbusplus::asio::connection& bus,
//                      const std::string& service, const std::string& path,
//                      const std::string& interface)
// {
//     using ReturnType = std::vector<std::pair<std::string, VariantType>>;
//     co_return co_await net::async_initiate<
//         net::use_awaitable_t<>,
//         ReturnTuple<ReturnType>(ReturnTuple<ReturnType>)>(
//         [&](auto handler) mutable {
//             bus.async_method_call(
//                 [handler = std::move(handler)](boost::system::error_code ec,
//                                                const ReturnType& data)
//                                                mutable {
//                     handler(ReturnTuple<ReturnType>{ec, data});
//                 },
//                 service, path, "org.freedesktop.DBus.Properties", "GetAll",
//                 interface);
//         },
//         mut_awaitable());
// }
// template <typename SubTreeType>
// inline AwaitableResult<SubTreeType>
//     getSubTree(sdbusplus::asio::connection& bus, const std::string& path,
//                int depth, const std::vector<std::string>& interfaces = {})
// {
//     using ReturnType = SubTreeType;
//     co_return co_await net::async_initiate<
//         net::use_awaitable_t<>,
//         ReturnTuple<ReturnType>(ReturnTuple<ReturnType>)>(
//         [&](auto handler) {
//             bus.async_method_call(
//                 [handler = std::move(handler)](boost::system::error_code ec,
//                                                ReturnType subtree) mutable {
//                     handler(ReturnTuple<ReturnType>{ec, std::move(subtree)});
//                 },
//                 "xyz.openbmc_project.ObjectMapper",
//                 "/xyz/openbmc_project/object_mapper",
//                 "xyz.openbmc_project.ObjectMapper", "GetSubTree", path,
//                 depth, interfaces);
//         },
//         mut_awaitable());
// }
