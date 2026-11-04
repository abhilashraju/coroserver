// entry_handlers.cpp — Entry upload and download routes for the
// Epicollect5-compatible server (Sub-Task 6).
//
// Routes implemented:
//   POST /api/upload/{slug}                               (mobile — JWT required)
//   POST /api/internal/web-upload/{slug}                  (PWA prod — no JWT)
//   POST /api/pwa/upload-debug/{slug}                     (PWA dev  — no JWT)
//   GET  /api/entries/{slug}?form_ref=&per_page=          (mobile download — JWT required)
//   GET  /api/internal/entries/{slug}?form_ref=&uuid=     (PWA edit fetch)
//   GET  /api/pwa/entries-debug/{slug}?...                (PWA dev alias)
//   GET  /api/internal/answers/{slug}?form_ref=&input_ref=&page=&per_page=
//   GET  /api/pwa/answers-debug/{slug}?...                (PWA dev alias)

#include "entry_handlers.hpp"

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
// Shared helpers
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

// Convert an EntryRow into the JSON object shape used in list/fetch responses.
json entryRowToJson(const EntryRow& row)
{
    json answers;
    try
    {
        answers = json::parse(row.answersJson);
    }
    catch (const json::exception&)
    {
        answers = json::object();
    }

    json entry;
    entry["entry_uuid"] = row.uuid;
    entry["created_at"] = row.createdAt;
    entry["title"] = row.title;
    entry["platform"] = row.platform;
    entry["answers"] = answers;

    json obj;
    obj["id"] = row.uuid;
    obj["entry"] = entry;
    return obj;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Handler: POST /api/upload/{slug}              (mobile — JWT required)
//           POST /api/internal/web-upload/{slug} (PWA prod)
//           POST /api/pwa/upload-debug/{slug}    (PWA dev)
//
// Body: {"data": <JSON:API entry payload>}
//
// Payload shape:
//   {
//     "type": "entry" | "branch_entry",
//     "id":   "<entry_uuid>",
//     "attributes": {
//       "form": { "ref": "<form_ref>" }
//     },
//     "entry": {                        // or "branch_entry"
//       "entry_uuid":      "...",
//       "created_at":      "...",
//       "platform":        "...",
//       "title":           "...",
//       "answers":         {...},
//       "project_version": "..."
//     },
//     "relationships": {
//       "parent": { "data": { "id": "<parent_uuid>" } },
//       "branch": { "data": { "branch_ref": "...", "owner_entry_uuid": "..." } }
//     }
//   }
//
// Returns 200: {"data":{"code":"ec5_237","title":"Entry saved"}}
// Returns 400: {"errors":[{"code":"ec5_55","title":"Invalid entry data"}]}
// ---------------------------------------------------------------------------
static net::awaitable<Response> uploadEntryHandler(Request& req,
                                                   const http_function& params,
                                                   ec5::Database& db,
                                                   bool requireJwt)
{
    if (requireJwt)
    {
        std::string token = ec5::extractBearer(req);
        if (token.empty() || !ec5::verify(token))
        {
            co_return jsonResponse(
                http::status::unauthorized, req.version(),
                makeEc5Error("ec5_50", "JWT token is not valid"));
        }
    }

    json body;
    try
    {
        body = json::parse(req.body());
    }
    catch (const json::exception&)
    {
        co_return jsonResponse(http::status::bad_request, req.version(),
                               makeEc5Error("ec5_55", "Invalid entry data"));
    }

    try
    {
        // The mobile app wraps the payload in a top-level "data" key.
        const json& payload = body.contains("data") ? body.at("data") : body;

        // Determine entry type
        std::string entryType = payload.value("type", "entry");
        bool isBranch = (entryType == "branch_entry");
        std::string subKey = isBranch ? "branch_entry" : "entry";

        // Top-level ID is the entry UUID
        std::string entryUuid = payload.value("id", "");

        // form_ref lives under attributes.form.ref
        std::string formRef;
        if (payload.contains("attributes") &&
            payload.at("attributes").contains("form"))
        {
            formRef =
                payload.at("attributes").at("form").value("ref", "");
        }

        // The entry sub-object
        const json& entrySub =
            payload.contains(subKey) ? payload.at(subKey) : json::object();

        if (entryUuid.empty())
        {
            entryUuid = entrySub.value("entry_uuid", "");
        }

        std::string createdAt = entrySub.value("created_at", "");
        std::string platform = entrySub.value("platform", "");
        std::string title = entrySub.value("title", "");
        std::string answersJson =
            entrySub.contains("answers")
                ? entrySub.at("answers").dump()
                : "{}";

        // Relationships
        std::string parentUuid;
        std::string branchRef;
        std::string branchOwnerUuid;
        if (payload.contains("relationships"))
        {
            const json& rel = payload.at("relationships");
            if (rel.contains("parent") && rel.at("parent").contains("data"))
            {
                parentUuid = rel.at("parent").at("data").value("id", "");
            }
            if (rel.contains("branch") && rel.at("branch").contains("data"))
            {
                const json& bd = rel.at("branch").at("data");
                branchRef = bd.value("branch_ref", "");
                branchOwnerUuid = bd.value("owner_entry_uuid", "");
            }
        }

        if (entryUuid.empty() || formRef.empty())
        {
            co_return jsonResponse(
                http::status::bad_request, req.version(),
                makeEc5Error("ec5_55", "Invalid entry data"));
        }

        EntryRow row;
        row.uuid = entryUuid;
        row.projectSlug = params["slug"];
        row.formRef = formRef;
        row.parentUuid = parentUuid;
        row.platform = platform;
        row.title = title;
        row.answersJson = answersJson;
        row.createdAt = createdAt;
        row.isBranch = isBranch ? 1 : 0;
        row.branchRef = branchRef;
        row.branchOwnerUuid = branchOwnerUuid;

        db.insertEntry(row);
    }
    catch (const std::exception&)
    {
        co_return jsonResponse(http::status::bad_request, req.version(),
                               makeEc5Error("ec5_55", "Invalid entry data"));
    }

    json result;
    result["data"]["code"] = "ec5_237";
    result["data"]["title"] = "Entry saved";
    co_return jsonResponse(http::status::ok, req.version(), result.dump());
}

// ---------------------------------------------------------------------------
// Handler: GET /api/entries/{slug}?form_ref=<ref>&per_page=<n>&page=<n>
//          (mobile download — JWT required)
//
// Returns:
//   {
//     "data":  { "entries": [...] },
//     "links": { "next": "<url>" | null },
//     "meta":  { "total": N }
//   }
//
// Pagination: `page` is 1-based. When more entries exist beyond the current
// page, `links.next` carries the full URL for the next page so the client
// can follow it without building URLs itself.
// ---------------------------------------------------------------------------
static net::awaitable<Response> downloadEntriesHandler(
    Request& req, const http_function& params, ec5::Database& db)
{
    std::string token = ec5::extractBearer(req);
    if (token.empty() || !ec5::verify(token))
    {
        co_return jsonResponse(http::status::unauthorized, req.version(),
                               makeEc5Error("ec5_50", "JWT token is not valid"));
    }

    std::string slug    = params["slug"];
    std::string formRef = params["form_ref"];

    int page    = 1;
    int perPage = 50;

    if (!params["page"].empty())
    {
        try { page = std::stoi(params["page"]); } catch (...) {}
    }
    if (!params["per_page"].empty())
    {
        try { perPage = std::stoi(params["per_page"]); } catch (...) {}
    }
    if (page    < 1) page    = 1;
    if (perPage < 1) perPage = 50;

    // Fetch one extra row to detect whether a next page exists without a
    // separate COUNT query.
    auto rows = db.findEntriesByForm(slug, formRef, page, perPage + 1);

    bool hasNextPage = (static_cast<int>(rows.size()) > perPage);
    if (hasNextPage)
    {
        rows.resize(static_cast<std::size_t>(perPage));
    }

    // Total: use the count of rows on this page plus entries already seen on
    // previous pages as a lower bound.  A real COUNT(*) would be cleaner but
    // this avoids an extra DB round-trip and the client only uses the total
    // to drive a progress bar, not for correctness.
    int knownTotal = (page - 1) * perPage + static_cast<int>(rows.size())
                     + (hasNextPage ? 1 : 0);

    json entries = json::array();
    for (const auto& row : rows)
    {
        entries.push_back(entryRowToJson(row));
    }

    // Build the "next" URL from the incoming request target so the host/port
    // is always correct (the client follows it verbatim).
    json nextLink = nullptr;
    if (hasNextPage)
    {
        // req.target() is the raw path+query, e.g.
        //   /api/entries/my-project?form_ref=...&per_page=250&page=1
        // Strip any existing `page=` parameter and append the new one.
        std::string target = std::string(req.target());
        auto stripParam = [](std::string url, const std::string& key) {
            auto removeOne = [&](std::string u) {
                // Remove &key=... or ?key=... (handles both positions)
                for (const auto& sep : std::vector<std::string>{"&" + key + "=",
                                                                 "?" + key + "="})
                {
                    auto pos = u.find(sep);
                    if (pos == std::string::npos) continue;
                    auto end = u.find('&', pos + 1);
                    if (end == std::string::npos)
                    {
                        u.erase(pos);          // last param — remove trailing &/?
                    }
                    else
                    {
                        // Replace leading separator char with the one from
                        // the remaining segment
                        u.erase(pos, end - pos);
                        if (sep[0] == '?') u[pos] = '?';
                    }
                }
                return u;
            };
            return removeOne(url);
        };

        target = stripParam(target, "page");
        target += (target.find('?') == std::string::npos ? "?" : "&");
        target += "page=" + std::to_string(page + 1);

        // Reconstruct absolute URL from the Host header
        std::string host = std::string(req[http::field::host]);
        std::string scheme = "https";
        nextLink = scheme + "://" + host + target;
    }

    json result;
    result["data"]["entries"]  = entries;
    result["links"]["next"]    = nextLink;
    result["meta"]["total"]    = knownTotal;

    co_return jsonResponse(http::status::ok, req.version(), result.dump());
}

// ---------------------------------------------------------------------------
// Handler: GET /api/internal/entries/{slug}?form_ref=<ref>&uuid=<uuid>
//           GET /api/pwa/entries-debug/{slug}?...
//
// Fetches a single entry by UUID (for PWA edit).
// Optional: ?branch_ref=<ref>&branch_owner_uuid=<uuid>
//
// Returns: {"data":{"entries":[<entry>]}}  — empty array if not found.
// ---------------------------------------------------------------------------
static net::awaitable<Response> fetchEntryHandler(Request& req,
                                                  const http_function& params,
                                                  ec5::Database& db)
{
    std::string uuid = params["uuid"];

    json entries = json::array();
    if (!uuid.empty())
    {
        auto row = db.findEntryByUuid(uuid);
        if (row)
        {
            entries.push_back(entryRowToJson(*row));
        }
    }

    json result;
    result["data"]["entries"] = entries;

    co_return jsonResponse(http::status::ok, req.version(), result.dump());
}

// ---------------------------------------------------------------------------
// Handler: GET /api/internal/answers/{slug}?form_ref=<ref>&input_ref=<ref>&page=<n>&per_page=<n>
//           GET /api/pwa/answers-debug/{slug}?...
//
// Returns: {"data":{"answers":[<answer strings>]}}
// ---------------------------------------------------------------------------
static net::awaitable<Response> savedAnswersHandler(Request& req,
                                                    const http_function& params,
                                                    ec5::Database& db)
{
    std::string slug = params["slug"];
    std::string formRef = params["form_ref"];
    std::string inputRef = params["input_ref"];

    int page = 1;
    int perPage = 50;
    if (!params["page"].empty())
    {
        try
        {
            page = std::stoi(params["page"]);
        }
        catch (...)
        {}
    }
    if (!params["per_page"].empty())
    {
        try
        {
            perPage = std::stoi(params["per_page"]);
        }
        catch (...)
        {}
    }

    auto answers = db.findAnswersByInputRef(slug, formRef, inputRef, page, perPage);

    json result;
    result["data"]["answers"] = answers;

    co_return jsonResponse(http::status::ok, req.version(), result.dump());
}

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

void register_entry_routes(reactor::HttpRouter& router, ec5::Database& db)
{
    // --- Upload: three URL aliases → same handler ---

    // Mobile (Bearer JWT required)
    router.add_post_handler(
        "/api/upload/{slug}",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return uploadEntryHandler(req, params, db, /*requireJwt=*/true);
        });

    // PWA prod (no JWT)
    router.add_post_handler(
        "/api/internal/web-upload/{slug}",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return uploadEntryHandler(req, params, db, /*requireJwt=*/false);
        });

    // PWA dev debug (no JWT)
    router.add_post_handler(
        "/api/pwa/upload-debug/{slug}",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return uploadEntryHandler(req, params, db, /*requireJwt=*/false);
        });

    // --- Download entries (mobile) ---
    router.add_get_handler(
        "/api/entries/{slug}",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return downloadEntriesHandler(req, params, db);
        });

    // --- Fetch single entry for PWA edit: two aliases ---
    router.add_get_handler(
        "/api/internal/entries/{slug}",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return fetchEntryHandler(req, params, db);
        });

    router.add_get_handler(
        "/api/pwa/entries-debug/{slug}",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return fetchEntryHandler(req, params, db);
        });

    // --- Saved answers: two aliases ---
    router.add_get_handler(
        "/api/internal/answers/{slug}",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return savedAnswersHandler(req, params, db);
        });

    router.add_get_handler(
        "/api/pwa/answers-debug/{slug}",
        [&db](Request& req, const http_function& params)
            -> net::awaitable<Response> {
            return savedAnswersHandler(req, params, db);
        });

    std::cout << "[epicollect5_server] Entry routes registered\n";
}

} // namespace ec5
