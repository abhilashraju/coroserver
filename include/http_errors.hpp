#pragma once
#include "beastdefs.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <stdexcept>
#include <type_traits>
struct file_not_found : std::runtime_error
{
    file_not_found(const std::string& error) :
        std::runtime_error("File Not Found:" + error)
    {}
};

inline Response make_file_not_found_error(const std::string& path, int version)
{
    Response res{http::status::not_found, version};
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(false);
    res.body() = std::format("File Not Found: {}", path);
    res.prepare_payload();
    return res;
}
inline Response make_internal_server_error(const std::string& message,
                                           int version)
{
    Response res{http::status::internal_server_error, version};
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(false);
    res.body() = std::format("Internal Server Error: {}", message);
    res.prepare_payload();
    return res;
}
inline Response make_bad_request_error(const std::string& message, int version)
{
    Response res{http::status::bad_request, version};
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(false);
    res.body() = std::format("Bad Request: {}", message);
    res.prepare_payload();
    return res;
}
inline Response make_unauthorized_error(const std::string& message, int version)
{
    Response res{http::status::unauthorized, version};
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(false);
    res.body() = std::format("Unauthorized: {}", message);
    res.prepare_payload();
    return res;
}
inline Response make_forbidden_error(const std::string& message, int version)
{
    Response res{http::status::forbidden, version};
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(false);
    res.body() = std::format("Forbidden: {}", message);
    res.prepare_payload();
    return res;
}
template <typename T>
concept ConvertibleToStringView =
    requires(T t) {
        { std::string_view(t) } -> std::convertible_to<std::string_view>;
    };

template <ConvertibleToStringView T>
inline Response
    make_success_response(T data, http::status st, int version,
                          std::string_view content_type = "plain/text")
{
    Response res{st, version};
    res.set(http::field::content_type, content_type);
    res.keep_alive(false);
    res.body() = std::string(std::string_view(data));
    res.prepare_payload();
    return res;
}
template <typename T>
concept IsNlohmannJson = std::is_same_v<T, nlohmann::json>;

template <IsNlohmannJson T>
inline Response make_success_response(const T& js, http::status st, int version)
{
    return make_success_response(js.dump(2), st, version, "application/json");
}
