#pragma once
#include "sdbus_calls.hpp"

#include <nlohmann/json.hpp>
using DbusVariantType = std::variant<
    std::vector<std::tuple<std::string, std::string, std::string>>,
    std::vector<std::string>, std::vector<double>, std::string, int64_t,
    uint64_t, double, int32_t, uint32_t, int16_t, uint16_t, uint8_t, bool,
    sdbusplus::message::unix_fd, std::vector<uint32_t>, std::vector<uint16_t>,
    sdbusplus::message::object_path,
    std::tuple<uint64_t,
               std::vector<std::tuple<std::string, double, uint64_t>>>,
    std::vector<sdbusplus::message::object_path>,
    std::vector<std::tuple<std::string, std::string>>,
    std::vector<std::tuple<uint32_t, std::vector<uint32_t>>>,
    std::vector<std::tuple<uint32_t, size_t>>,
    std::vector<std::tuple<
        std::vector<std::tuple<sdbusplus::message::object_path, std::string>>,
        std::string, std::string, uint64_t>>>;

// clang-format on
using DBusPropertiesMap = std::vector<std::pair<std::string, DbusVariantType>>;
using DBusInterfacesMap =
    std::vector<std::pair<std::string, DBusPropertiesMap>>;
using ManagedObjectType =
    std::vector<std::pair<sdbusplus::message::object_path, DBusInterfacesMap>>;

// Map of service name to list of interfaces
using MapperServiceMap =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

// Map of object paths to MapperServiceMaps
using MapperGetSubTreeResponse =
    std::vector<std::pair<std::string, MapperServiceMap>>;

using MapperGetObject =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

using MapperGetAncestorsResponse = std::vector<
    std::pair<std::string,
              std::vector<std::pair<std::string, std::vector<std::string>>>>>;

using MapperGetSubTreePathsResponse = std::vector<std::string>;

using MapperEndPoints = std::vector<std::string>;

inline nlohmann::json toJson(const std::vector<std::string>& paths)
{
    nlohmann::json jsonPaths = nlohmann::json::array();
    for (const auto& path : paths)
    {
        jsonPaths.push_back(path);
    }
    return jsonPaths;
}
inline nlohmann::json toJson(const MapperGetObject& objectNames)
{
    nlohmann::json jsonObjects = nlohmann::json::array();
    for (const auto& [path, interfaces] : objectNames)
    {
        nlohmann::json jsonObject;
        jsonObject["path"] = path;
        nlohmann::json jsonInterfaces = nlohmann::json::array();
        for (const auto& interface : interfaces)
        {
            jsonInterfaces.push_back(interface);
        }
        jsonObject["interfaces"] = jsonInterfaces;
        jsonObjects.push_back(jsonObject);
    }
    return jsonObjects;
}
inline nlohmann::json toJson(const MapperGetSubTreeResponse& subtree)
{
    nlohmann::json jsonSubtree = nlohmann::json::array();
    for (const auto& [path, services] : subtree)
    {
        nlohmann::json jsonPath;
        jsonPath["path"] = path;
        nlohmann::json jsonServices = nlohmann::json::array();
        for (const auto& [service, interfaces] : services)
        {
            nlohmann::json jsonService;
            jsonService["service"] = service;
            nlohmann::json jsonInterfaces = nlohmann::json::array();
            for (const auto& interface : interfaces)
            {
                jsonInterfaces.push_back(interface);
            }
            jsonService["interfaces"] = jsonInterfaces;
            jsonServices.push_back(jsonService);
        }
        jsonPath["services"] = jsonServices;
        jsonSubtree.push_back(jsonPath);
    }
    return jsonSubtree;
}
inline nlohmann::json toJson(const ManagedObjectType& objects)
{
    nlohmann::json jsonObjects = nlohmann::json::array();
    for (const auto& [path, interfaces] : objects)
    {
        nlohmann::json jsonObject;
        jsonObject["path"] = path;
        nlohmann::json jsonInterfaces = nlohmann::json::array();
        for (const auto& [interface, properties] : interfaces)
        {
            nlohmann::json jsonInterface;
            jsonInterface["interface"] = interface;
            nlohmann::json jsonProperties = nlohmann::json::array();
            for (const auto& [property, value] : properties)
            {
                nlohmann::json jsonProperty;
                jsonProperty["property"] = property;
                nlohmann::json jsonValue;
                std::visit(
                    [&jsonValue](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::string> ||
                                      std::is_arithmetic_v<T>)
                        {
                            jsonValue = arg;
                        }
                    },
                    value);
                jsonProperty["value"] = jsonValue;
                jsonProperties.push_back(jsonProperty);
            }
            jsonInterface["properties"] = jsonProperties;
            jsonInterfaces.push_back(jsonInterface);
        }
        jsonObject["interfaces"] = jsonInterfaces;
        jsonObjects.push_back(jsonObject);
    }
    return jsonObjects;
}
inline sdbusplus::message::object_path toDbusPath(const std::string& path)
{
    return sdbusplus::message::object_path(path);
}
