// media_handlers.cpp — Media file upload, delete, and serve routes for the
// Epicollect5-compatible server (Sub-Task 7).
//
// Routes implemented:
//   POST /api/internal/upload-file/{slug}            (PWA prod)
//   POST /api/pwa/upload-file-debug/{slug}           (PWA dev alias)
//   POST /api/internal/temp-media-delete/{slug}      (PWA prod)
//   POST /api/pwa/temp-media-delete-debug/{slug}     (PWA dev alias)
//   GET  /api/media/{slug}?type=&name=&format=       (file serving)

#include "media_handlers.hpp"

#include "cors_middleware.hpp"
#include "multipart_parser.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace reactor;
using json = nlohmann::json;
namespace fs = std::filesystem;

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

/// Return the EC5_MEDIA_PATH env var value, defaulting to "./media".
std::string mediaRoot()
{
    const char* env = std::getenv("EC5_MEDIA_PATH");
    return env ? std::string(env) : "./media";
}

/// Map a file extension to a MIME type string.
std::string mimeForExtension(const std::string& ext)
{
    if (ext == ".jpg" || ext == ".jpeg")
    {
        return "image/jpeg";
    }
    if (ext == ".png")
    {
        return "image/png";
    }
    if (ext == ".mp4")
    {
        return "video/mp4";
    }
    if (ext == ".m4a" || ext == ".aac")
    {
        return "audio/mp4";
    }
    if (ext == ".mp3")
    {
        return "audio/mpeg";
    }
    if (ext == ".gif")
    {
        return "image/gif";
    }
    return "application/octet-stream";
}

/// Read an entire file into a std::string (binary).
/// Returns an empty optional on error.
std::optional<std::string> readFileBinary(const fs::path& path)
{
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open())
    {
        return std::nullopt;
    }
    auto size = ifs.tellg();
    if (size < 0)
    {
        return std::nullopt;
    }
    ifs.seekg(0, std::ios::beg);
    std::string buf(static_cast<std::size_t>(size), '\0');
    ifs.read(buf.data(), size);
    return buf;
}

// ---------------------------------------------------------------------------
// Handler: POST /api/internal/upload-file/{slug}
//          POST /api/pwa/upload-file-debug/{slug}
// ---------------------------------------------------------------------------
net::awaitable<Response> uploadFileHandler(Request& req,
                                           const http_function& params,
                                           ec5::Database& db)
{
    const int ver = req.version();

    // Retrieve slug from URL params
    const std::string slug = params["slug"];
    if (slug.empty())
    {
        co_return jsonResponse(http::status::bad_request, ver,
                               makeEc5Error("ec5_11", "Missing slug"));
    }

    // Extract boundary from Content-Type header
    std::string ctHeader{req[http::field::content_type]};
    const std::string boundary = extractBoundary(ctHeader);
    if (boundary.empty())
    {
        co_return jsonResponse(http::status::bad_request, ver,
                               makeEc5Error("ec5_11", "Missing multipart boundary"));
    }

    // Parse multipart body
    const std::string& bodyStr = req.body();
    auto parts = parseMultipart(bodyStr, boundary);

    // Extract fields from parts
    std::string entryUuid;
    std::string filetype;
    std::string filename;
    std::string fileData;
    std::string fileContentType;

    for (const auto& part : parts)
    {
        if (part.name == "entry_uuid")
        {
            entryUuid = part.data;
        }
        else if (part.name == "type")
        {
            filetype = part.data;
        }
        else if (!part.filename.empty())
        {
            // This is the file part
            filename = part.filename;
            fileData = part.data;
            fileContentType = part.contentType;
        }
    }

    if (filename.empty() || fileData.empty())
    {
        co_return jsonResponse(http::status::bad_request, ver,
                               makeEc5Error("ec5_11", "No file part found in upload"));
    }

    // Build storage path: {mediaRoot}/temp/{slug}/{filename}
    const fs::path storagePath =
        fs::path(mediaRoot()) / "temp" / slug / filename;
    std::error_code ec;
    fs::create_directories(storagePath.parent_path(), ec);
    if (ec)
    {
        std::cerr << "[media] Failed to create directories "
                  << storagePath.parent_path() << ": " << ec.message() << "\n";
        co_return jsonResponse(http::status::internal_server_error, ver,
                               makeEc5Error("ec5_11", "Failed to create media directory"));
    }

    // Write file bytes to disk
    {
        std::ofstream ofs(storagePath, std::ios::binary);
        if (!ofs.is_open())
        {
            co_return jsonResponse(http::status::internal_server_error, ver,
                                   makeEc5Error("ec5_11", "Failed to open file for writing"));
        }
        ofs.write(fileData.data(), static_cast<std::streamsize>(fileData.size()));
    }

    // Insert DB record (is_temp = true)
    try
    {
        db.insertMedia(entryUuid, slug, filename, filetype,
                       storagePath.string(), /*isTemp=*/true);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[media] insertMedia failed: " << ex.what() << "\n";
        // Non-fatal — the file is already on disk
    }

    json result;
    result["data"]["code"] = "ec5_265";
    result["data"]["title"] = "File saved";
    co_return jsonResponse(http::status::ok, ver, result.dump());
}

// ---------------------------------------------------------------------------
// Handler: POST /api/internal/temp-media-delete/{slug}
//          POST /api/pwa/temp-media-delete-debug/{slug}
// ---------------------------------------------------------------------------
net::awaitable<Response> tempMediaDeleteHandler(Request& req,
                                                const http_function& params,
                                                ec5::Database& db)
{
    const int ver = req.version();

    const std::string slug = params["slug"];

    // Parse JSON body
    // Expected: {"data":{"type":"delete","id":<entry_uuid>,"delete":{"filetype":<type>,"filename":<name>}}}
    std::string entryUuid;
    std::string filetype;
    std::string filename;

    try
    {
        json body = json::parse(req.body());
        auto& data = body.at("data");
        entryUuid = data.value("id", std::string{});
        auto& del = data.at("delete");
        filetype = del.value("filetype", std::string{});
        filename = del.value("filename", std::string{});
    }
    catch (const std::exception& ex)
    {
        co_return jsonResponse(http::status::bad_request, ver,
                               makeEc5Error("ec5_11", "Invalid request body"));
    }

    if (filename.empty() || filetype.empty())
    {
        co_return jsonResponse(http::status::bad_request, ver,
                               makeEc5Error("ec5_11", "Missing filename or filetype"));
    }

    // Look up storage path from DB
    auto mediaRow = db.findMedia(slug, filename, filetype);
    if (mediaRow)
    {
        // Delete file from disk
        std::error_code ec;
        fs::remove(fs::path(mediaRow->storagePath), ec);
        if (ec)
        {
            std::cerr << "[media] remove failed for " << mediaRow->storagePath
                      << ": " << ec.message() << "\n";
        }
        db.deleteMedia(slug, filename, filetype);
    }
    else
    {
        // Try constructing the temp path directly as a fallback
        const fs::path guessedPath =
            fs::path(mediaRoot()) / "temp" / slug / filename;
        std::error_code ec;
        fs::remove(guessedPath, ec);
    }

    json result;
    result["data"]["code"] = "ec5_266";
    result["data"]["title"] = "File deleted";
    co_return jsonResponse(http::status::ok, ver, result.dump());
}

// ---------------------------------------------------------------------------
// Handler: GET /api/media/{slug}?type=<photo|audio|video>&name=<filename>&format=<fmt>
// ---------------------------------------------------------------------------
net::awaitable<Response> serveMediaHandler(Request& req,
                                           const http_function& params,
                                           ec5::Database& db)
{
    const int ver = req.version();

    const std::string slug = params["slug"];

    // Parse query parameters from the target URL
    const std::string target{req.target()};
    auto parseQuery = [&](const std::string& key) -> std::string {
        const std::string needle = key + "=";
        auto qpos = target.find('?');
        if (qpos == std::string::npos)
        {
            return {};
        }
        auto p = target.find(needle, qpos);
        if (p == std::string::npos)
        {
            return {};
        }
        p += needle.size();
        auto end = target.find('&', p);
        return target.substr(p, end == std::string::npos ? std::string::npos : end - p);
    };

    const std::string name = parseQuery("name");
    if (name.empty())
    {
        co_return jsonResponse(http::status::bad_request, ver,
                               makeEc5Error("ec5_130", "Missing name parameter"));
    }

    // Try non-temp path first, then temp
    const fs::path nonTempPath = fs::path(mediaRoot()) / slug / name;
    const fs::path tempPath = fs::path(mediaRoot()) / "temp" / slug / name;

    fs::path resolvedPath;
    if (fs::exists(nonTempPath))
    {
        resolvedPath = nonTempPath;
    }
    else if (fs::exists(tempPath))
    {
        resolvedPath = tempPath;
    }
    else
    {
        co_return jsonResponse(http::status::not_found, ver,
                               makeEc5Error("ec5_130", "Media not found"));
    }

    auto fileBytes = readFileBinary(resolvedPath);
    if (!fileBytes)
    {
        co_return jsonResponse(http::status::not_found, ver,
                               makeEc5Error("ec5_130", "Media not found"));
    }

    // Determine MIME type from extension
    const std::string ext = resolvedPath.extension().string();
    std::string mime = mimeForExtension(ext);

    Response res{http::status::ok, ver};
    res.set(http::field::content_type, mime);
    res.keep_alive(false);
    res.body() = std::move(*fileBytes);
    res.prepare_payload();
    co_return res;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// register_media_routes
// ---------------------------------------------------------------------------

void register_media_routes(reactor::HttpRouter& router, ec5::Database& db)
{
    // Ensure the base media directory exists at startup
    const fs::path root = mediaRoot();
    std::error_code ec;
    fs::create_directories(root / "temp", ec);
    if (ec)
    {
        std::cerr << "[media] Warning: could not create media directory "
                  << root << ": " << ec.message() << "\n";
    }

    // POST upload-file — PWA prod + debug
    router.add_post_handler(
        "/api/internal/upload-file/{slug}",
        [&db](Request& req, const http_function& params) {
            return uploadFileHandler(req, params, db);
        });
    router.add_post_handler(
        "/api/pwa/upload-file-debug/{slug}",
        [&db](Request& req, const http_function& params) {
            return uploadFileHandler(req, params, db);
        });

    // POST temp-media-delete — PWA prod + debug
    router.add_post_handler(
        "/api/internal/temp-media-delete/{slug}",
        [&db](Request& req, const http_function& params) {
            return tempMediaDeleteHandler(req, params, db);
        });
    router.add_post_handler(
        "/api/pwa/temp-media-delete-debug/{slug}",
        [&db](Request& req, const http_function& params) {
            return tempMediaDeleteHandler(req, params, db);
        });

    // GET media file serving
    router.add_get_handler(
        "/api/media/{slug}",
        [&db](Request& req, const http_function& params) {
            return serveMediaHandler(req, params, db);
        });

    std::cout << "[epicollect5_server] Media routes registered\n";
}

} // namespace ec5
