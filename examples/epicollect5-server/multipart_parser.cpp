// multipart_parser.cpp — Minimal multipart/form-data parser.
//
// Implements RFC 2046 section 5.1 multipart parsing using only the C++
// standard library.  No external dependencies.

#include "multipart_parser.hpp"

#include <algorithm>
#include <sstream>

namespace ec5
{

// ---------------------------------------------------------------------------
// extractBoundary
// ---------------------------------------------------------------------------

std::string extractBoundary(const std::string& contentTypeHeader)
{
    // Look for "boundary=" (case-insensitive search via tolower copy)
    const std::string needle = "boundary=";
    std::string lower = contentTypeHeader;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });

    auto pos = lower.find(needle);
    if (pos == std::string::npos)
    {
        return {};
    }
    pos += needle.size();

    // Boundary may be quoted or unquoted; strip optional quotes
    std::string boundary;
    if (pos < contentTypeHeader.size() && contentTypeHeader[pos] == '"')
    {
        ++pos; // skip opening quote
        auto end = contentTypeHeader.find('"', pos);
        if (end == std::string::npos)
        {
            end = contentTypeHeader.size();
        }
        boundary = contentTypeHeader.substr(pos, end - pos);
    }
    else
    {
        // Unquoted — read until ';', whitespace, or end
        auto end = pos;
        while (end < contentTypeHeader.size() &&
               contentTypeHeader[end] != ';' &&
               contentTypeHeader[end] != ' ' &&
               contentTypeHeader[end] != '\t' &&
               contentTypeHeader[end] != '\r' &&
               contentTypeHeader[end] != '\n')
        {
            ++end;
        }
        boundary = contentTypeHeader.substr(pos, end - pos);
    }
    return boundary;
}

// ---------------------------------------------------------------------------
// Internal: parse a block of part headers (text before \r\n\r\n).
// Returns a MultipartPart with name, filename, contentType filled in;
// data is left empty for the caller to fill.
// ---------------------------------------------------------------------------
static MultipartPart parsePartHeaders(const std::string& headerBlock)
{
    MultipartPart part;
    std::istringstream stream(headerBlock);
    std::string line;

    while (std::getline(stream, line))
    {
        // Strip trailing \r if present
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            break;
        }

        // Normalise header name to lowercase for comparison
        auto colonPos = line.find(':');
        if (colonPos == std::string::npos)
        {
            continue;
        }
        std::string headerName = line.substr(0, colonPos);
        std::string headerValue = line.substr(colonPos + 1);

        // Trim leading space from value
        while (!headerValue.empty() &&
               (headerValue.front() == ' ' || headerValue.front() == '\t'))
        {
            headerValue.erase(headerValue.begin());
        }

        std::string lowerName = headerName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](unsigned char c) { return static_cast<char>(::tolower(c)); });

        if (lowerName == "content-disposition")
        {
            // Parse: form-data; name="field"; filename="file.jpg"
            auto extractParam =
                [&](const std::string& param) -> std::string {
                const std::string needle = param + "=\"";
                auto p = headerValue.find(needle);
                if (p == std::string::npos)
                {
                    return {};
                }
                p += needle.size();
                auto end = headerValue.find('"', p);
                if (end == std::string::npos)
                {
                    end = headerValue.size();
                }
                return headerValue.substr(p, end - p);
            };
            part.name = extractParam("name");
            part.filename = extractParam("filename");
        }
        else if (lowerName == "content-type")
        {
            part.contentType = headerValue;
        }
    }
    return part;
}

// ---------------------------------------------------------------------------
// parseMultipart
// ---------------------------------------------------------------------------

std::vector<MultipartPart> parseMultipart(const std::string& body,
                                          const std::string& boundary)
{
    std::vector<MultipartPart> parts;
    if (boundary.empty() || body.empty())
    {
        return parts;
    }

    // Each part boundary is "--" + boundary
    const std::string delimiter = "--" + boundary;
    const std::string finalDelimiter = delimiter + "--";

    std::size_t pos = 0;

    // Find the first delimiter
    pos = body.find(delimiter, pos);
    if (pos == std::string::npos)
    {
        return parts;
    }
    // Skip past the first delimiter + CRLF
    pos += delimiter.size();
    if (pos < body.size() && body[pos] == '\r')
    {
        ++pos;
    }
    if (pos < body.size() && body[pos] == '\n')
    {
        ++pos;
    }

    while (pos < body.size())
    {
        // Find the next delimiter
        auto nextDelim = body.find(delimiter, pos);
        if (nextDelim == std::string::npos)
        {
            break;
        }

        // The part content runs from pos to just before the next delimiter.
        // Strip the CRLF that precedes the boundary.
        std::size_t partEnd = nextDelim;
        if (partEnd >= 2 && body[partEnd - 2] == '\r' &&
            body[partEnd - 1] == '\n')
        {
            partEnd -= 2;
        }
        else if (partEnd >= 1 && body[partEnd - 1] == '\n')
        {
            --partEnd;
        }

        // Split part into header block and body on the blank line (\r\n\r\n)
        const std::string blankLine = "\r\n\r\n";
        auto blankPos = body.find(blankLine, pos);
        if (blankPos == std::string::npos || blankPos >= nextDelim)
        {
            // Fallback: try bare \n\n
            const std::string blankLineN = "\n\n";
            blankPos = body.find(blankLineN, pos);
            if (blankPos == std::string::npos || blankPos >= nextDelim)
            {
                break;
            }
            MultipartPart part =
                parsePartHeaders(body.substr(pos, blankPos - pos));
            std::size_t dataStart = blankPos + blankLineN.size();
            part.data = body.substr(dataStart, partEnd - dataStart);
            parts.push_back(std::move(part));
        }
        else
        {
            MultipartPart part =
                parsePartHeaders(body.substr(pos, blankPos - pos));
            std::size_t dataStart = blankPos + blankLine.size();
            part.data = body.substr(dataStart, partEnd - dataStart);
            parts.push_back(std::move(part));
        }

        // Advance past the delimiter we already found
        pos = nextDelim + delimiter.size();

        // Check if this was the final boundary ("--")
        if (pos + 1 < body.size() && body[pos] == '-' && body[pos + 1] == '-')
        {
            break;
        }

        // Skip CRLF after boundary
        if (pos < body.size() && body[pos] == '\r')
        {
            ++pos;
        }
        if (pos < body.size() && body[pos] == '\n')
        {
            ++pos;
        }
    }

    return parts;
}

} // namespace ec5
