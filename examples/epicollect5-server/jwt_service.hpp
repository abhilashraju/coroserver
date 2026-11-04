#pragma once

#include "beastdefs.hpp"

#include <optional>
#include <string>

namespace ec5
{

/// Parsed claims extracted from a verified JWT.
struct Claims
{
    std::string email;
    std::string userId;
};

/// Issue a signed HS256 JWT for the given email and userId.
/// Tokens expire after 24 hours.
std::string issue(const std::string& email, const std::string& userId);

/// Verify a JWT token.  Returns the parsed Claims on success, or
/// std::nullopt if the signature is invalid, the token is malformed,
/// or the token has expired.
std::optional<Claims> verify(const std::string& token);

/// Extract the bearer token from the Authorization header of a request.
/// Returns an empty string if the header is absent or malformed.
std::string extractBearer(const reactor::Request& req);

} // namespace ec5
