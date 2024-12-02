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
};
