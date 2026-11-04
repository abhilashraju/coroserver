#pragma once
// multipart_parser.hpp — Minimal multipart/form-data parser for the
// Epicollect5-compatible server (Sub-Task 7).

#include <string>
#include <vector>

namespace ec5
{

/// A single part from a multipart/form-data body.
struct MultipartPart
{
    std::string name;        // value of `name` in Content-Disposition
    std::string filename;    // value of `filename` in Content-Disposition (empty for fields)
    std::string contentType; // value of Content-Type header for this part (empty if absent)
    std::string data;        // raw body bytes of this part
};

/// Extract the boundary token from a Content-Type header value such as
///   "multipart/form-data; boundary=----WebKitFormBoundaryXYZ"
/// Returns the boundary string, or an empty string if not found.
std::string extractBoundary(const std::string& contentTypeHeader);

/// Split `body` on `--boundary` and parse each part into a MultipartPart.
/// Handles CRLF (\r\n) line endings as required by RFC 2046.
std::vector<MultipartPart> parseMultipart(const std::string& body,
                                          const std::string& boundary);

} // namespace ec5
