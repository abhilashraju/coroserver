#include "jwt_service.hpp"

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string_view>
#include <vector>

namespace ec5
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace
{

/// Return the JWT signing secret.  Reads EC5_JWT_SECRET from the environment;
/// falls back to a hardcoded development default.
std::string_view jwtSecret()
{
    static const std::string secret = []() -> std::string {
        const char* env = std::getenv("EC5_JWT_SECRET");
        return env ? std::string(env) : std::string("ec5-dev-secret-changeme");
    }();
    return secret;
}

/// Encode binary data as Base64url (no padding, RFC 4648 §5).
std::string base64urlEncode(const unsigned char* data, std::size_t len)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    for (std::size_t i = 0; i < len; i += 3)
    {
        const unsigned int b0 = data[i];
        const unsigned int b1 = (i + 1 < len) ? data[i + 1] : 0u;
        const unsigned int b2 = (i + 2 < len) ? data[i + 2] : 0u;

        out += alphabet[(b0 >> 2) & 0x3F];
        out += alphabet[((b0 << 4) | (b1 >> 4)) & 0x3F];
        if (i + 1 < len)
            out += alphabet[((b1 << 2) | (b2 >> 6)) & 0x3F];
        if (i + 2 < len)
            out += alphabet[b2 & 0x3F];
    }
    return out;
}

std::string base64urlEncodeStr(const std::string& s)
{
    return base64urlEncode(
        reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

/// Decode a Base64url string (no padding) into bytes.
/// Returns an empty vector on error.
std::vector<unsigned char> base64urlDecode(const std::string& input)
{
    static const signed char lookup[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0-15
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 16-31
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, // 32-47 (45='-')
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, // 48-63 (0-9)
        -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, // 64-79 (A-O)
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, 63, // 80-95 (P-Z, '_')
        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, // 96-111 (a-o)
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, // 112-127 (p-z)
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };

    std::vector<unsigned char> out;
    out.reserve((input.size() * 3) / 4 + 1);

    int accum = 0;
    int bits = 0;
    for (unsigned char ch : input)
    {
        const int val = lookup[ch];
        if (val < 0)
            return {}; // invalid character
        accum = (accum << 6) | val;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((accum >> bits) & 0xFF));
        }
    }
    return out;
}

/// Compute HMAC-SHA256 of message with key.
std::vector<unsigned char> hmacSha256(std::string_view key,
                                      std::string_view message)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digestLen = 0;
    HMAC(EVP_sha256(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(message.data()),
         message.size(),
         digest, &digestLen);
    return std::vector<unsigned char>(digest, digest + digestLen);
}

/// Constant-time compare (same as CRYPTO_memcmp semantics).
bool constTimeEqual(const std::vector<unsigned char>& a,
                    const std::vector<unsigned char>& b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        diff |= (a[i] ^ b[i]);
    return diff == 0;
}

/// Parse a simple JSON string value for a given key.
/// Handles only the flat key:"value" pattern produced by issue().
std::string jsonGetString(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos)
        return {};
    pos += needle.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos)
        return {};
    return json.substr(pos, end - pos);
}

/// Parse a simple JSON numeric value for a given key.
long long jsonGetInt(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos)
        return -1;
    pos += needle.size();
    return std::stoll(json.substr(pos));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string issue(const std::string& email, const std::string& userId)
{
    const std::string header  = R"({"alg":"HS256","typ":"JWT"})";
    const std::string encHeader = base64urlEncodeStr(header);

    const long long now = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    const long long exp = now + 86400; // 24 hours

    // Build payload manually to avoid pulling in a JSON library.
    std::ostringstream payloadStream;
    payloadStream << R"({"sub":")" << email
                  << R"(","uid":")"  << userId
                  << R"(","exp":)"   << exp
                  << R"(,"iat":)"    << now
                  << "}";
    const std::string payload    = payloadStream.str();
    const std::string encPayload = base64urlEncodeStr(payload);

    const std::string signingInput = encHeader + "." + encPayload;
    const auto        sigBytes     = hmacSha256(jwtSecret(), signingInput);
    const std::string encSig       = base64urlEncode(sigBytes.data(), sigBytes.size());

    return signingInput + "." + encSig;
}

std::optional<Claims> verify(const std::string& token)
{
    // Split into three parts.
    const auto dot1 = token.find('.');
    if (dot1 == std::string::npos)
        return std::nullopt;
    const auto dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos)
        return std::nullopt;

    const std::string encHeader  = token.substr(0, dot1);
    const std::string encPayload = token.substr(dot1 + 1, dot2 - dot1 - 1);
    const std::string encSig     = token.substr(dot2 + 1);

    // Verify signature.
    const std::string signingInput = encHeader + "." + encPayload;
    const auto        expected     = hmacSha256(jwtSecret(), signingInput);

    const auto receivedBytes = base64urlDecode(encSig);
    if (!constTimeEqual(expected, receivedBytes))
        return std::nullopt;

    // Decode and parse payload.
    const auto payloadBytes = base64urlDecode(encPayload);
    if (payloadBytes.empty())
        return std::nullopt;

    const std::string payload(payloadBytes.begin(), payloadBytes.end());

    // Check expiry.
    const long long exp = jsonGetInt(payload, "exp");
    const long long now = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    if (exp < 0 || now > exp)
        return std::nullopt;

    // Extract claims.
    Claims claims;
    claims.email  = jsonGetString(payload, "sub");
    claims.userId = jsonGetString(payload, "uid");
    if (claims.email.empty() || claims.userId.empty())
        return std::nullopt;

    return claims;
}

std::string extractBearer(const reactor::Request& req)
{
    const auto authField = req[reactor::http::field::authorization];
    if (authField.empty())
        return {};

    constexpr std::string_view prefix = "Bearer ";
    if (authField.substr(0, prefix.size()) != prefix)
        return {};

    return std::string(authField.substr(prefix.size()));
}

} // namespace ec5
