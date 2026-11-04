// utility_handlers.cpp — Utility routes (uniqueness validation & OpenCage proxy
// stub) for the Epicollect5-compatible server (Sub-Task 8).
//
// Routes implemented:
//   POST /api/internal/unique-answer/{slug}              (PWA prod)
//   POST /api/pwa/unique-answer-debug/{slug}             (PWA dev alias)
//   GET  /api/internal/proxies/opencage/{encodedAddress} (PWA prod)
//   GET  /api/pwa/proxies/opencage-debug/{encodedAddress}(PWA dev alias)

#include "utility_handlers.hpp"

#include "logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>

using namespace reactor;
using json = nlohmann::json;

namespace ec5
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

std::string makeEc5Error(const std::string& code, const std::string& title)
{
    json j;
    j["errors"] = json::array({{{"code", code}, {"title", title}}});
    return j.dump();
}

Response jsonResponse(http::status status, int version, const std::string& body)
{
    Response res{status, version};
    res.set(http::field::content_type, "application/json");
    res.keep_alive(false);
    res.body() = body;
    res.prepare_payload();
    return res;
}

std::string urlDecode(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] == '%')
        {
            if (i + 2 < in.size())
            {
                int val = 0;
                std::istringstream hexStream(in.substr(i + 1, 2));
                if (hexStream >> std::hex >> val)
                {
                    out += static_cast<char>(val);
                    i += 2;
                }
                else
                {
                    out += '%';
                }
            }
            else
            {
                out += '%';
            }
        }
        else if (in[i] == '+')
        {
            out += ' ';
        }
        else
        {
            out += in[i];
        }
    }
    return out;
}

std::string toLower(const std::string& s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Handler: POST /api/internal/unique-answer/{slug}
//           POST /api/pwa/unique-answer-debug/{slug}
//
// Request body JSON shape sent by the PWA
// (JSONTransformerService.makeUniqueEntry):
//   {
//     "data": {
//       "id": "<entry_uuid>",
//       "type": "entry" | "branch_entry",
//       "attributes": {
//         "form": { "ref": "<form_ref>", "type": "hierarchy" }
//       },
//       "relationships": { ... },
//       "entry" | "branch_entry": {
//         "entry_uuid": "<entry_uuid>",
//         "input_ref": "<input_ref>",
//         "answer": "<answer_value>",
//         "project_version": "<version>",
//         "platform": "..."
//       }
//     }
//   }
//
// Also supports flat / fallback payload formats:
//   { "data": { "form_ref": "...", "input_ref": "...", "answer_value": "...",
//   "entry_uuid": "..." } }
//
// Response:
//   Unique:    HTTP 200 {"data":{"code":"ec5_249"}}
//   Duplicate: HTTP 422 {"errors":[{"code":"ec5_22","title":"Answer already
//   used"}]}
// ---------------------------------------------------------------------------
static net::awaitable<Response> uniqueAnswerHandler(
    Request& req, const http_function& params, ec5::Database& db)
{
    std::string slug = params["slug"];

    json body;
    try
    {
        body = json::parse(req.body());
    }
    catch (const json::exception&)
    {
        co_return jsonResponse(http::status::bad_request, req.version(),
                               makeEc5Error("ec5_55", "Invalid request body"));
    }

    const json& data = body.contains("data") ? body.at("data") : body;

    std::string formRef;
    std::string inputRef;
    std::string answerValue;
    std::string entryUuid;

    // 1. Check top-level JSON:API structure
    if (data.contains("id") && data["id"].is_string())
    {
        entryUuid = data["id"].get<std::string>();
    }

    if (data.contains("attributes") && data["attributes"].is_object() &&
        data["attributes"].contains("form") &&
        data["attributes"]["form"].is_object())
    {
        formRef = data["attributes"]["form"].value("ref", "");
    }

    std::string entryType = data.value("type", "entry");
    std::string innerKey =
        (entryType == "branch_entry") ? "branch_entry" : "entry";

    const json* innerData = nullptr;
    if (data.contains(innerKey) && data[innerKey].is_object())
    {
        innerData = &data[innerKey];
    }
    else if (data.contains("entry") && data["entry"].is_object())
    {
        innerData = &data["entry"];
    }
    else if (data.contains("branch_entry") && data["branch_entry"].is_object())
    {
        innerData = &data["branch_entry"];
    }

    if (innerData)
    {
        if (inputRef.empty())
        {
            inputRef = innerData->value("input_ref", "");
        }
        if (entryUuid.empty())
        {
            entryUuid = innerData->value("entry_uuid", "");
        }
        if (innerData->contains("answer"))
        {
            if ((*innerData)["answer"].is_string())
            {
                answerValue = (*innerData)["answer"].get<std::string>();
            }
            else
            {
                answerValue = (*innerData)["answer"].dump();
            }
        }
        else if (innerData->contains("answer_value"))
        {
            if ((*innerData)["answer_value"].is_string())
            {
                answerValue = (*innerData)["answer_value"].get<std::string>();
            }
            else
            {
                answerValue = (*innerData)["answer_value"].dump();
            }
        }
    }

    // 2. Direct / fallback keys
    if (formRef.empty())
    {
        formRef = data.value("form_ref", "");
    }
    if (inputRef.empty())
    {
        inputRef = data.value("input_ref", "");
    }
    if (entryUuid.empty())
    {
        entryUuid = data.value("entry_uuid", "");
    }
    if (answerValue.empty() && data.contains("answer"))
    {
        if (data["answer"].is_string())
        {
            answerValue = data["answer"].get<std::string>();
        }
        else
        {
            answerValue = data["answer"].dump();
        }
    }
    if (answerValue.empty() && data.contains("answer_value"))
    {
        if (data["answer_value"].is_string())
        {
            answerValue = data["answer_value"].get<std::string>();
        }
        else
        {
            answerValue = data["answer_value"].dump();
        }
    }

    LOG_INFO(
        "Unique answer check: slug={}, formRef={}, inputRef={}, answerValue={}, entryUuid={}",
        slug, formRef, inputRef, answerValue, entryUuid);

    // Retrieve entries for this project and form to verify uniqueness
    auto entries = db.findEntriesByForm(slug, formRef, 1, 1000);

    std::string lowerTarget = toLower(answerValue);
    bool isDuplicate = false;

    for (const auto& entry : entries)
    {
        // Exclude the entry currently being edited
        if (!entryUuid.empty() && entry.uuid == entryUuid)
        {
            continue;
        }

        try
        {
            json answersObj = json::parse(entry.answersJson);
            if (answersObj.contains(inputRef))
            {
                const auto& val = answersObj[inputRef];
                std::string existingStr;
                if (val.is_object() && val.contains("answer"))
                {
                    if (val["answer"].is_string())
                    {
                        existingStr = val["answer"].get<std::string>();
                    }
                    else
                    {
                        existingStr = val["answer"].dump();
                    }
                }
                else if (val.is_string())
                {
                    existingStr = val.get<std::string>();
                }
                else
                {
                    existingStr = val.dump();
                }

                if (toLower(existingStr) == lowerTarget)
                {
                    isDuplicate = true;
                    break;
                }
            }
        }
        catch (const json::exception&)
        {
            // Skip unparseable answers
        }
    }

    if (isDuplicate)
    {
        co_return jsonResponse(http::status::unprocessable_entity,
                               req.version(),
                               makeEc5Error("ec5_22", "Answer already used"));
    }

    // Return unique status code ec5_249 (and ec5_236 compatible)
    json resData;
    resData["data"]["code"] = "ec5_249";
    co_return jsonResponse(http::status::ok, req.version(), resData.dump());
}

// ---------------------------------------------------------------------------
// Handler: GET /api/internal/proxies/opencage/{encodedAddress}
//           GET /api/pwa/proxies/opencage-debug/{encodedAddress}
//
// Geocoding proxy stub returning fixed coordinates (London, UK).
// ---------------------------------------------------------------------------
static net::awaitable<Response> opencageProxyHandler(
    Request& req, const http_function& params)
{
    std::string encodedAddress = params["encodedAddress"];
    std::string decodedAddress = urlDecode(encodedAddress);

    LOG_INFO("OpenCage geocode request for address: {} (raw: {})",
             decodedAddress, encodedAddress);

    json resJson;
    resJson["status"]["code"] = 200;
    resJson["results"] =
        json::array({{{"geometry", {{"lng", -0.1276}, {"lat", 51.5074}}},
                      {"formatted", "London, UK"}}});

    co_return jsonResponse(http::status::ok, req.version(), resJson.dump());
}

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

void register_utility_routes(reactor::HttpRouter& router, ec5::Database& db)
{
    // Unique answer validation
    router.add_post_handler(
        "/api/internal/unique-answer/{slug}",
        [&db](Request& req,
              const http_function& params) -> net::awaitable<Response> {
            return uniqueAnswerHandler(req, params, db);
        });

    router.add_post_handler(
        "/api/pwa/unique-answer-debug/{slug}",
        [&db](Request& req,
              const http_function& params) -> net::awaitable<Response> {
            return uniqueAnswerHandler(req, params, db);
        });

    // OpenCage geocoding proxy stub
    router.add_get_handler(
        "/api/internal/proxies/opencage/{encodedAddress}",
        [](Request& req,
           const http_function& params) -> net::awaitable<Response> {
            return opencageProxyHandler(req, params);
        });

    router.add_get_handler(
        "/api/pwa/proxies/opencage-debug/{encodedAddress}",
        [](Request& req,
           const http_function& params) -> net::awaitable<Response> {
            return opencageProxyHandler(req, params);
        });
}

} // namespace ec5
