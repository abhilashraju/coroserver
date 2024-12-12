#pragma once
#include "dbus_types.hpp"
#include "http_server.hpp"
#include "logger.hpp"

struct DbusHandlers
{
    sdbusplus::asio::connection& conn;
    DbusHandlers(sdbusplus::asio::connection& conn, HttpRouter& router) :
        conn(conn)
    {
        router.add_get_handler(
            "/getObjects", std::bind_front(&DbusHandlers::getDbusObject, this));
        router.add_get_handler(
            "/getSubTree", std::bind_front(&DbusHandlers::getSubTree, this));
        router.add_get_handler(
            "/getSubTreePaths",
            std::bind_front(&DbusHandlers::getSubTree, this));
        router.add_get_handler(
            "/getAncestors",
            std::bind_front(&DbusHandlers::getAncestors, this));
        router.add_get_handler(
            "/getAssociationEndpoints",
            std::bind_front(&DbusHandlers::getAssociationEndpoints, this));
        router.add_get_handler(
            "/getManagedObjects",
            std::bind_front(&DbusHandlers::getManagedObjects, this));
        router.add_get_handler(
            "/getAssociatedSubTree",
            std::bind_front(&DbusHandlers::getAssociatedSubTree, this));
        router.add_get_handler(
            "/introspect", std::bind_front(&DbusHandlers::introspect, this));
        router.add_get_handler(
            "/getProperty",
            std::bind_front(&DbusHandlers::getDbusProperty, this));
        router.add_post_handler(
            "/setProperty",
            std::bind_front(&DbusHandlers::setDbusProperty, this));
    }

    net::awaitable<Response> getSubTree(Request& req,
                                        const http_function& params)
    {
        nlohmann::json data;

        data = nlohmann::json::parse(req.body(), nullptr, false);

        if (data.is_discarded())
        {
            co_return make_bad_request_error("Invalid JSON", req.version());
        }

        auto [ec, subtree] = co_await ::getSubTree<MapperGetSubTreeResponse>(
            conn, data["path"], data["depth"], data["interfaces"]);
        if (ec)
        {
            LOG_ERROR("Error getting subtree: {}", ec.message());
            co_return make_internal_server_error("Internal Server Error",
                                                 req.version());
        }
        nlohmann::json jsonResponse = toJson(subtree);
        co_return make_success_response(jsonResponse, http::status::ok,
                                        req.version());
    }
    net::awaitable<Response> getSubTreePaths(Request& req,
                                             const http_function& params)
    {
        nlohmann::json data;

        data = nlohmann::json::parse(req.body(), nullptr, false);

        if (data.is_discarded())
        {
            co_return make_bad_request_error("Invalid JSON", req.version());
        }

        auto [ec, subtree] =
            co_await ::getSubTreePaths<MapperGetSubTreePathsResponse>(
                conn, data["path"], data["depth"], data["interfaces"]);
        if (ec)
        {
            LOG_ERROR("Error getting subtree: {}", ec.message());
            co_return make_internal_server_error("Internal Server Error",
                                                 req.version());
        }
        nlohmann::json jsonResponse = toJson(subtree);
        co_return make_success_response(jsonResponse, http::status::ok,
                                        req.version());
    }
    net::awaitable<Response>
        getAssociatedSubTree(Request& req, const http_function& params)
    {
        nlohmann::json data;

        data = nlohmann::json::parse(req.body(), nullptr, false);

        if (data.is_discarded())
        {
            co_return make_bad_request_error("Invalid JSON", req.version());
        }

        auto [ec, subtree] =
            co_await ::getAssociatedSubTree<MapperGetSubTreeResponse>(
                conn, toDbusPath(data["associatedPath"]),
                toDbusPath(data["path"]), data["depth"], data["interfaces"]);
        if (ec)
        {
            LOG_ERROR("Error getting subtree: {}", ec.message());
            co_return make_internal_server_error("Internal Server Error",
                                                 req.version());
        }
        nlohmann::json jsonResponse = toJson(subtree);
        co_return make_success_response(jsonResponse, http::status::ok,
                                        req.version());
    }
    net::awaitable<Response>
        getAssociatedSubTreePaths(Request& req, const http_function& params)
    {
        nlohmann::json data;

        data = nlohmann::json::parse(req.body(), nullptr, false);

        if (data.is_discarded())
        {
            co_return make_bad_request_error("Invalid JSON", req.version());
        }

        auto [ec, subtree] =
            co_await ::getAssociatedSubTreePaths<MapperGetSubTreePathsResponse>(
                conn, toDbusPath(data["associatedPath"]),
                toDbusPath(data["path"]), data["depth"], data["interfaces"]);
        if (ec)
        {
            LOG_ERROR("Error getting subtree: {}", ec.message());
            co_return make_internal_server_error("Internal Server Error",
                                                 req.version());
        }
        nlohmann::json jsonResponse = toJson(subtree);
        co_return make_success_response(jsonResponse, http::status::ok,
                                        req.version());
    }
    net::awaitable<Response> getAncestors(Request& req,
                                          const http_function& params)
    {
        nlohmann::json data;

        data = nlohmann::json::parse(req.body(), nullptr, false);

        if (data.is_discarded())
        {
            co_return make_bad_request_error("Invalid JSON", req.version());
        }

        auto [ec, ancestors] =
            co_await ::getAncestors<MapperGetAncestorsResponse>(
                conn, data["path"], data["interfaces"]);
        if (ec)
        {
            LOG_ERROR("Error getting ancestors: {}", ec.message());
            co_return make_internal_server_error("Internal Server Error",
                                                 req.version());
        }
        nlohmann::json jsonResponse = toJson(ancestors);
        co_return make_success_response(jsonResponse, http::status::ok,
                                        req.version());
    }
    net::awaitable<Response>
        getAssociationEndpoints(Request& req, const http_function& params)
    {
        nlohmann::json data;

        data = nlohmann::json::parse(req.body(), nullptr, false);

        if (data.is_discarded())
        {
            co_return make_bad_request_error("Invalid JSON", req.version());
        }

        auto [ec, endpoints] =
            co_await ::getAssociationEndPoints<MapperEndPoints>(conn,
                                                                data["path"]);
        if (ec)
        {
            LOG_ERROR("Error getting endpoints: {}", ec.message());
            co_return make_internal_server_error("Internal Server Error",
                                                 req.version());
        }
        nlohmann::json jsonResponse = toJson(endpoints);
        co_return make_success_response(jsonResponse, http::status::ok,
                                        req.version());
    }
    net::awaitable<Response> getManagedObjects(Request& req,
                                               const http_function& params)
    {
        nlohmann::json data;

        data = nlohmann::json::parse(req.body(), nullptr, false);

        if (data.is_discarded())
        {
            co_return make_bad_request_error("Invalid JSON", req.version());
        }

        auto [ec, objects] = co_await ::getManagedObjects<MapperGetObject>(
            conn, data["service"], toDbusPath(data["path"]));
        if (ec)
        {
            LOG_ERROR("Error getting objects: {}", ec.message());
            co_return make_internal_server_error("Internal Server Error",
                                                 req.version());
        }
        nlohmann::json jsonResponse = toJson(objects);
        co_return make_success_response(jsonResponse, http::status::ok,
                                        req.version());
    }
    net::awaitable<Response> getDbusObject(Request& req,
                                           const http_function& params)
    {
        nlohmann::json data;

        data = nlohmann::json::parse(req.body(), nullptr, false);

        if (data.is_discarded())
        {
            co_return make_bad_request_error("Invalid JSON", req.version());
        }

        auto [ec, object] = co_await ::getDbusObject<MapperGetObject>(
            conn, data["path"], data["interfaces"]);
        if (ec)
        {
            LOG_ERROR("Error getting object: {}", ec.message());
            co_return make_internal_server_error("Internal Server Error",
                                                 req.version());
        }
        nlohmann::json jsonResponse = toJson(object);
        co_return make_success_response(jsonResponse, http::status::ok,
                                        req.version());
    }
    net::awaitable<Response> introspect(Request& req,
                                        const http_function& params)
    {
        nlohmann::json data = nlohmann::json::parse(req.body(), nullptr, false);

        if (data.is_discarded())
        {
            co_return make_bad_request_error("Invalid JSON", req.version());
        }

        auto [ec, introspection] = co_await ::introspect(
            conn, data["service"], toDbusPath(data["path"]));
        if (ec)
        {
            LOG_ERROR("Error introspecting: {}", ec.message());
            co_return make_internal_server_error("Internal Server Error",
                                                 req.version());
        }
        LOG_INFO("introspected {}", introspection);
        co_return make_success_response(xmlToJson(introspection),
                                        http::status::ok, req.version());
    }

    template <typename T>
    static net::awaitable<boost::system::error_code> setProperty(
        sdbusplus::asio::connection& conn, const std::string& service,
        const std::string& path, const std::string& interface,
        const std::string& property, const nlohmann::json& jValue)
    {
        T value = jValue;
        auto [ec] = co_await ::setProperty(conn, service, path, interface,
                                           property, value);
        co_return ec;
    }
    using PropertySetter =
        std::function<net::awaitable<boost::system::error_code>(
            sdbusplus::asio::connection& conn, const std::string& service,
            const std::string& path, const std::string& interface,
            const std::string& property, const nlohmann::json& jValue)>;
    std::map<std::string, PropertySetter> propertySetters = {
        {"b", &DbusHandlers::setProperty<bool>},
        {"i", &DbusHandlers::setProperty<int>},
        {"s", &DbusHandlers::setProperty<std::string>},
        {"ai", &DbusHandlers::setProperty<std::vector<int>>},
        {"as", &DbusHandlers::setProperty<std::vector<std::string>>},
    };
    net::awaitable<Response> setDbusProperty(Request& req,
                                             const http_function& params)
    {
        nlohmann::json data = nlohmann::json::parse(req.body(), nullptr, false);

        if (data.is_discarded())
        {
            co_return make_bad_request_error("Invalid JSON", req.version());
        }
        std::string signature = data["signature"];
        if (propertySetters.contains(signature))
        {
            auto setter = propertySetters[signature];
            auto ec = co_await setter(conn, data["service"], data["path"],
                                      data["interface"], data["property"],
                                      data["value"]);
            if (ec)
            {
                LOG_ERROR("Error setting DBus property: {}", ec.message());
                co_return make_internal_server_error("Internal Server Error",
                                                     req.version());
            }
            nlohmann::json jsonResponse;
            jsonResponse["status"] = "success";
            co_return make_success_response(jsonResponse, http::status::ok,
                                            req.version());
        }

        co_return make_bad_request_error("Unsupported signature type",
                                         req.version());
    }

    using PropertyGetter = std::function<net::awaitable<nlohmann::json>(
        sdbusplus::asio::connection& conn, const std::string& service,
        const std::string& path, const std::string& interface,
        const std::string& property)>;
    template <typename T>
    static net::awaitable<nlohmann::json>
        getProperty(sdbusplus::asio::connection& conn,
                    const std::string& service, const std::string& path,
                    const std::string& interface, const std::string& property)
    {
        auto [ec, value] =
            co_await ::getProperty<T>(conn, service, path, interface, property);
        nlohmann::json ret;
        if (ec)
        {
            co_return ret;
        }
        ret["property"] = value;
        co_return ret;
    }
    std::map<std::string, PropertyGetter> propertyGetters = {
        {"b", &DbusHandlers::getProperty<bool>},
        {"i", &DbusHandlers::getProperty<int>},
        {"s", &DbusHandlers::getProperty<std::string>},
        {"ai", &DbusHandlers::getProperty<std::vector<int>>},
        {"as", &DbusHandlers::getProperty<std::vector<std::string>>}};
    net::awaitable<Response> getDbusProperty(Request& req,
                                             const http_function& params)
    {
        nlohmann::json data = nlohmann::json::parse(req.body(), nullptr, false);

        if (data.is_discarded())
        {
            co_return make_bad_request_error("Invalid JSON", req.version());
        }
        std::string signature = data["signature"];
        if (propertyGetters.contains(signature))
        {
            auto getter = propertyGetters[signature];
            auto result = co_await getter(conn, data["service"], data["path"],
                                          data["interface"], data["property"]);
            if (result.empty())
            {
                LOG_ERROR("Error getting DBus property");
                co_return make_internal_server_error("Internal Server Error",
                                                     req.version());
            }
            co_return make_success_response(result, http::status::ok,
                                            req.version());
        }
    }
};
