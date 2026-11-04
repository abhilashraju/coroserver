// project_handlers.cpp — Project routes for the Epicollect5-compatible server
// (Sub-Task 5).
//
// Routes implemented:
//   GET  /api/project/{slug}               (mobile WEBVIEW)
//   GET  /api/internal/project/{slug}      (PWA prod)
//   GET  /api/pwa/project-debug/{slug}     (PWA dev debug)
//   GET  /api/projects/{searchTerm}        (search, optional ?exact=true)
//   GET  /api/project-version/{slug}       (JWT-protected version check)
//   POST /api/admin/projects               (dev helper — seed test projects)

#include "project_handlers.hpp"

#include "cors_middleware.hpp"
#include "jwt_service.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

using namespace reactor;
using json = nlohmann::json;

namespace ec5
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace
{

std::string makeEc5Error(const std::string& code, const std::string& title)
{
    json j;
    j["errors"] = json::array({{{"code", code}, {"title", title}}});
    return j.dump();
}

Response jsonResponse(http::status status, int version,
                      const std::string& body)
{
    Response res{status, version};
    res.set(http::field::content_type, "application/json");
    ec5::addCorsHeaders(res);
    res.keep_alive(false);
    res.body() = body;
    res.prepare_payload();
    return res;
}

} // namespace

// ---------------------------------------------------------------------------
// Handler: GET /api/project/{slug}
//          GET /api/internal/project/{slug}
//          GET /api/pwa/project-debug/{slug}
//
// Returns definition_json stored in the DB directly — it must already be
// shaped as: {"data":{"project":{...}},"meta":{...}}
// ---------------------------------------------------------------------------
static net::awaitable<Response> projectBySlugHandler(
    Request& req, const http_function& params, ec5::Database& db)
{
    std::string slug = params["slug"];

    auto row = db.findProjectBySlug(slug);
    if (!row)
    {
        co_return jsonResponse(http::status::not_found, req.version(),
                               makeEc5Error("ec5_11", "Project not found"));
    }

    co_return jsonResponse(http::status::ok, req.version(),
                           row->definitionJson);
}

// ---------------------------------------------------------------------------
// Handler: GET /api/projects/{searchTerm}
//          GET /api/projects/{searchTerm}?exact=true
//
// Returns: {"data":[{"slug":"...","name":"...","ref":"..."},...]}
// ---------------------------------------------------------------------------
static net::awaitable<Response> searchProjectsHandler(
    Request& req, const http_function& params, ec5::Database& db)
{
    std::string term = params["searchTerm"];
    bool exact = (params["exact"] == "true");

    auto rows = db.searchProjects(term, exact);

    json result;
    result["data"] = json::array();
    for (const auto& r : rows)
    {
        result["data"].push_back(
            {{"project", {{"slug", r.slug}, {"name", r.name}, {"ref", r.ref}}}});
    }

    co_return jsonResponse(http::status::ok, req.version(), result.dump());
}

// ---------------------------------------------------------------------------
// Handler: GET /api/project-version/{slug}
//
// Requires a valid Bearer JWT.
// Returns: {"data":{"attributes":{"structure_last_updated":"..."}}}
// ---------------------------------------------------------------------------
static net::awaitable<Response> projectVersionHandler(
    Request& req, const http_function& params, ec5::Database& db)
{
    std::string token = ec5::extractBearer(req);
    if (token.empty() || !ec5::verify(token))
    {
        co_return jsonResponse(
            http::status::unauthorized, req.version(),
            makeEc5Error("ec5_50", "JWT token is not valid"));
    }

    std::string slug = params["slug"];
    auto row = db.findProjectBySlug(slug);
    if (!row)
    {
        co_return jsonResponse(http::status::not_found, req.version(),
                               makeEc5Error("ec5_11", "Project not found"));
    }

    json result;
    result["data"]["attributes"]["structure_last_updated"] =
        row->structureLastUpdated;

    co_return jsonResponse(http::status::ok, req.version(), result.dump());
}

// ---------------------------------------------------------------------------
// Handler: POST /api/admin/projects
//
// Dev helper — seeds a project into the database.
//
// Accepts two body shapes for definition_json:
//   1. Nested object  (preferred):
//        { "slug":"...", ..., "definition_json": { "data":{...}, "meta":{...} } }
//   2. Pre-serialised string (legacy):
//        { "slug":"...", ..., "definition_json": "{\"data\":{...}}" }
//
// Returns 201: {"data":{"created":true}}
// ---------------------------------------------------------------------------
static net::awaitable<Response> adminInsertProjectHandler(
    Request& req, const http_function& /*params*/, ec5::Database& db)
{
    json body;
    try
    {
        body = json::parse(req.body());
    }
    catch (const json::exception&)
    {
        co_return jsonResponse(http::status::bad_request, req.version(),
                               makeEc5Error("ec5_99", "Invalid JSON body"));
    }

    // Accept definition_json as either a nested object or a pre-serialised
    // string so callers don't need to double-encode.
    std::string definitionJsonStr;
    try
    {
        const auto& djField = body.at("definition_json");
        if (djField.is_string())
        {
            // Already serialised — use as-is (validate it parses cleanly)
            definitionJsonStr = djField.get<std::string>();
            json::parse(definitionJsonStr); // throws if malformed
        }
        else
        {
            // Nested object — serialise it
            definitionJsonStr = djField.dump();
        }
    }
    catch (const json::exception& e)
    {
        co_return jsonResponse(
            http::status::bad_request, req.version(),
            makeEc5Error("ec5_99",
                         std::string("Invalid definition_json: ") + e.what()));
    }

    try
    {
        db.upsertProject(body.at("slug").get<std::string>(),
                         body.at("name").get<std::string>(),
                         body.at("ref").get<std::string>(),
                         definitionJsonStr,
                         body.at("structure_last_updated").get<std::string>());
    }
    catch (const std::exception& e)
    {
        co_return jsonResponse(
            http::status::internal_server_error, req.version(),
            makeEc5Error("ec5_99",
                         std::string("Upsert failed: ") + e.what()));
    }

    json result;
    result["data"]["created"] = true;
    co_return jsonResponse(http::status::created, req.version(),
                           result.dump());
}

// ---------------------------------------------------------------------------
// Handler: GET /api/admin/projects
//
// Dev helper — returns all projects as a summary list.
// Response: {"data":[{"slug":"...","name":"...","ref":"...","structure_last_updated":"..."},...]}
// ---------------------------------------------------------------------------
static net::awaitable<Response> adminListProjectsHandler(
    Request& req, const http_function& /*params*/, ec5::Database& db)
{
    auto rows = db.listAllProjects();

    json result;
    result["data"] = json::array();
    for (const auto& r : rows)
    {
        result["data"].push_back({{"slug", r.slug},
                                  {"name", r.name},
                                  {"ref", r.ref},
                                  {"structure_last_updated",
                                   r.structureLastUpdated}});
    }

    co_return jsonResponse(http::status::ok, req.version(), result.dump());
}

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

void register_project_routes(reactor::HttpRouter& router, ec5::Database& db)
{
    // Project definition — three URL aliases → same handler
    router.add_get_handler(
        "/api/project/{slug}",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return projectBySlugHandler(req, params, db);
        });

    router.add_get_handler(
        "/api/internal/project/{slug}",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return projectBySlugHandler(req, params, db);
        });

    router.add_get_handler(
        "/api/pwa/project-debug/{slug}",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return projectBySlugHandler(req, params, db);
        });

    // Project search (query param ?exact=true lands in params["exact"])
    router.add_get_handler(
        "/api/projects/{searchTerm}",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return searchProjectsHandler(req, params, db);
        });

    // Project version check — requires Bearer JWT
    router.add_get_handler(
        "/api/project-version/{slug}",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return projectVersionHandler(req, params, db);
        });

    // Admin: list all projects
    router.add_get_handler(
        "/api/admin/projects",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return adminListProjectsHandler(req, params, db);
        });

    // Admin: seed / update a project (dev helper, not used by the mobile app)
    router.add_post_handler(
        "/api/admin/projects",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return adminInsertProjectHandler(req, params, db);
        });

    std::cout << "[epicollect5_server] Project routes registered\n";
}

} // namespace ec5
