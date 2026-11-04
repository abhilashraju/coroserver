#pragma once
// error_helpers.hpp — Epicollect5-shaped JSON error response helper for the
// Epicollect5-compatible server (Sub-Task 9).

#include "cors_middleware.hpp"
#include "http_server.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace ec5
{

/// Build an Epicollect5 JSON error response:
///   {"errors":[{"code":"<code>","title":"<title>"}]}
/// CORS headers are included so browser preflight errors are handled too.
inline reactor::Response makeEc5Error(const std::string& code,
                                      const std::string& title,
                                      reactor::http::status status,
                                      int version)
{
    nlohmann::json body = {
        {"errors", {{{"code", code}, {"title", title}}}}};

    reactor::Response res{status, version};
    res.set(reactor::http::field::content_type, "application/json");
    addCorsHeaders(res);
    res.body() = body.dump();
    res.prepare_payload();
    return res;
}

} // namespace ec5
