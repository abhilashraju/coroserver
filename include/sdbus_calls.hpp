#pragma once
#include "make_awaitable.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/asio/sd_event.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/timer.hpp>

template <typename... RetTypes, typename... InputArgs>
inline auto awaitable_dbus_method_call(
    sdbusplus::asio::connection& conn, const std::string& service,
    const std::string& objpath, const std::string& interf,
    const std::string& method,
    const InputArgs&... a) -> AwaitableResult<RetTypes...>
{
    auto h = make_awaitable_handler<RetTypes...>([&](auto promise) {
        conn.async_method_call(
            [promise = std::move(promise)](boost::system::error_code ec,
                                           RetTypes... values) mutable {
                promise.setValues(ec, std::move(values)...);
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
        make_awaitable_handler<boost::system::error_code>([&](auto promise) {
            sdbusplus::asio::setProperty(
                conn, service, objpath, interf, property, value,
                [promise = std::move(promise)](
                    boost::system::error_code ec) mutable {
                    promise.setValues(ec);
                });
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
    auto h = make_awaitable_handler<ReturnType>([&](auto promise) {
        bus.async_method_call(
            [promise = std::move(promise)](boost::system::error_code ec,
                                           const ReturnType& data) mutable {
                promise.setValues(ec, data);
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
    auto h = make_awaitable_handler<SubTreeType>([&](auto promise) {
        bus.async_method_call(
            [promise = std::move(promise)](boost::system::error_code ec,
                                           SubTreeType subtree) mutable {
                promise.setValues(ec, std::move(subtree));
            },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree", path, depth,
            interfaces);
    });
    co_return co_await h();
}
