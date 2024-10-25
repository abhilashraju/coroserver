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
template <typename... Types>
using ResultType = net::awaitable<PrependEC<Types...>>;

template <typename... RetTypes, typename... InputArgs>
inline auto awaitable_method_call(
    sdbusplus::asio::connection& conn, const std::string& service,
    const std::string& objpath, const std::string& interf,
    const std::string& method, const InputArgs&... a) -> ResultType<RetTypes...>
{
    boost::system::error_code ec;
    co_return co_await net::async_initiate<
        net::use_awaitable_t<>, PrependEC<RetTypes...>(PrependEC<RetTypes...>)>(
        [&](auto handler) {
            conn.async_method_call(
                [handler = std::move(handler)](boost::system::error_code ec,
                                               RetTypes... values) mutable {
                    handler(PrependEC<RetTypes...>{ec, std::move(values)...});
                },
                service, objpath, interf, method, a...);
        },
        mut_awaitable());
}
using BoostError = std::tuple<boost::system::error_code>;

template <typename Type>
inline ResultType<Type>
    getProperty(sdbusplus::asio::connection& conn, const std::string& service,
                const std::string& objpath, const std::string& interf,
                const std::string& property)
{
    auto [ec, value] =
        co_await awaitable_method_call<std::variant<std::monostate, Type>>(
            conn, service, objpath, "org.freedesktop.DBus.Properties", "Get",
            interf, property);
    co_return std::make_tuple(ec, std::get<Type>(value));
}

template <typename InputArgs>
inline net::awaitable<BoostError>
    setProperty(sdbusplus::asio::connection& conn, const std::string& service,
                const std::string& objpath, const std::string& interf,
                const std::string& property, const InputArgs& value)
{
    // co_return co_await awaitable_method_call<>(
    //     conn, service, objpath, "org.freedesktop.DBus.Properties", "Set",
    //     interf, property, value);
    co_return co_await net::async_initiate<net::use_awaitable_t<>,
                                           BoostError(BoostError)>(
        [&](auto handler) {
            sdbusplus::asio::setProperty(
                conn, service, objpath, interf, property, value,
                [handler = std::move(handler)](
                    boost::system::error_code ec) mutable {
                    handler(BoostError{ec});
                });
        },
        mut_awaitable());
}

template <typename VariantType>
inline ResultType<std::vector<std::pair<std::string, VariantType>>>
    getAllProperties(sdbusplus::asio::connection& bus,
                     const std::string& service, const std::string& path,
                     const std::string& interface)
{
    using ReturnType = std::vector<std::pair<std::string, VariantType>>;
    co_return co_await net::async_initiate<
        net::use_awaitable_t<>, PrependEC<ReturnType>(PrependEC<ReturnType>)>(
        [&](auto handler) mutable {
            bus.async_method_call(
                [handler = std::move(handler)](boost::system::error_code ec,
                                               const ReturnType& data) mutable {
                    handler(PrependEC<ReturnType>{ec, data});
                },
                service, path, "org.freedesktop.DBus.Properties", "GetAll",
                interface);
        },
        mut_awaitable());
}
template <typename SubTreeType>
inline ResultType<SubTreeType>
    getSubTree(sdbusplus::asio::connection& bus, const std::string& path,
               int depth, const std::vector<std::string>& interfaces = {})
{
    using ReturnType = SubTreeType;
    co_return co_await net::async_initiate<
        net::use_awaitable_t<>, PrependEC<ReturnType>(PrependEC<ReturnType>)>(
        [&](auto handler) {
            bus.async_method_call(
                [handler = std::move(handler)](boost::system::error_code ec,
                                               ReturnType subtree) mutable {
                    handler(PrependEC<ReturnType>{ec, std::move(subtree)});
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree", path, depth,
                interfaces);
        },
        mut_awaitable());
}
