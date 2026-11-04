#pragma once
// cors_middleware.hpp — CORS header helpers for the Epicollect5-compatible
// server (Sub-Task 9).

#include "http_server.hpp"

namespace ec5
{

/// Add the standard CORS headers to `res`.
inline void addCorsHeaders(reactor::Response& res)
{
    res.set("Access-Control-Allow-Origin", "*");
    res.set("Access-Control-Allow-Methods",
            "GET, POST, PUT, DELETE, OPTIONS");
    res.set("Access-Control-Allow-Headers",
            "Content-Type, Authorization, X-Requested-With");
    res.set("Access-Control-Max-Age", "86400");
}

/// Build a 204 No Content preflight response with CORS headers attached.
inline reactor::Response makePreflightResponse(int version)
{
    reactor::Response res{reactor::http::status::no_content, version};
    addCorsHeaders(res);
    res.prepare_payload();
    return res;
}

} // namespace ec5
