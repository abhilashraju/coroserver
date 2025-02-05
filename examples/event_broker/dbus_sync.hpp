#pragma once
#include "eventmethods.hpp"
#include "eventqueue.hpp"
#include "logger.hpp"
#include "sdbus_calls.hpp"
#include "utilities.hpp"

#include <ranges>
struct DbusSync
{
    DbusSync(sdbusplus::asio::connection& conn, EventQueue& eventQueue) :
        conn(conn), eventQueue(eventQueue)
    {
        eventQueue.addEventConsumer(
            "PropertyChanged", std::bind_front(&DbusSync::dbusConsumer, this));
    }
    void addToSync(const std::string& service, const std::string& path,
                   const std::string& interface, const std::string& property)
    {
        std::string matchRule =
            sdbusplus::bus::match::rules::propertiesChanged(path, interface);
        auto propcallback = [this, service, property,
                             path](sdbusplus::message::message& msg) {
            std::string interfaceName;
            std::map<std::string, std::variant<std::string>> changedProperties;
            std::vector<std::string> invalidatedProperties;

            msg.read(interfaceName, changedProperties, invalidatedProperties);

            LOG_INFO("Properties changed on interface: {}", interfaceName);

            changedProperties |
                std::ranges::views::filter(

                    [&property](const auto& p) { return p.first == property; });
            for (const auto& [prop, value] : changedProperties)
            {
                auto event =
                    makeEvent("PropertyChanged",
                              service + ":" + path + ":" + interfaceName + ":" +
                                  prop + ":" + std::get<std::string>(value));
                if (!eventQueue.eventExists(event))
                {
                    eventQueue.addEvent(event);
                }
            }
        };
        matches.emplace_back(conn, matchRule.c_str(), std::move(propcallback));
    }
    net::awaitable<boost::system::error_code>
        dbusConsumer(Streamer stream, const std::string& event)
    {
        LOG_DEBUG("Received event: {}", event);
        auto datas = split(event, ':', 1);
        std::string service(datas[0]);
        std::string path(datas[1]);
        std::string interface(datas[2]);
        std::string prop(datas[3]);
        std::string value(datas[4]);
        auto [ec] =
            co_await setProperty(conn, service, path, interface, prop, value);
        if (ec)
        {
            LOG_ERROR("Failed to set property: {}", ec.message());
        }
        co_return boost::system::error_code{};
    }
    sdbusplus::asio::connection& conn;
    EventQueue& eventQueue;
    std::vector<sdbusplus::bus::match::match> matches;
};
