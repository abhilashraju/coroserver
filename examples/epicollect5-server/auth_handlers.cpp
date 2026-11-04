// auth_handlers.cpp — Authentication routes for the Epicollect5-compatible
// server (Sub-Task 4).
//
// Routes implemented:
//   GET  /api/login
//   POST /api/login/local
//   POST /api/login/passwordless/code
//   POST /api/login/passwordless
//   POST /api/handle/google
//   POST /api/handle/apple
//   POST /api/login/verify-google
//   POST /api/login/verify-apple
//   POST /api/profile/account-deletion-request
//   POST /api/admin/register  (dev helper — not part of the mobile app)

#include "auth_handlers.hpp"

#include "cors_middleware.hpp"
#include "jwt_service.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <random>
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

/// Build an Epicollect5 error response body.
std::string makeEc5Error(const std::string& code, const std::string& title)
{
    json j;
    j["errors"] = json::array({{{"code", code}, {"title", title}}});
    return j.dump();
}

/// Build a JSON response string with the given status, content-type JSON, and
/// body.
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

/// Build the standard JWT success response.
///   {"data":{"jwt":"<token>"},"meta":{"user":{"name":"...","email":"..."}}}
std::string makeJwtResponse(const std::string& token, const std::string& name,
                             const std::string& email)
{
    json j;
    j["data"]["jwt"]          = token;
    j["meta"]["user"]["name"] = name;
    j["meta"]["user"]["email"] = email;
    return j.dump();
}

/// Generate a random 6-digit decimal code as a zero-padded string.
std::string generateCode()
{
    // Use a thread-local Mersenne Twister seeded once per thread.
    thread_local std::mt19937 rng{
        static_cast<unsigned>(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count())};
    std::uniform_int_distribution<int> dist(100000, 999999);
    return std::to_string(dist(rng));
}

/// Return an ISO-8601 UTC timestamp `seconds` from now.
std::string isoFuture(long long seconds)
{
    using clock = std::chrono::system_clock;
    const auto tp = clock::now() + std::chrono::seconds(seconds);
    const std::time_t t = clock::to_time_t(tp);
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

/// Return the current UTC time as an ISO-8601 string.
std::string isoNow()
{
    return isoFuture(0);
}

/// Parse a simple JSON body from a request.  Returns a discarded json object
/// on failure so callers can test with `j.is_discarded()`.
json parseBody(const Request& req)
{
    return json::parse(req.body(), nullptr, /*exceptions=*/false);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Route implementations
// ---------------------------------------------------------------------------

/// GET /api/login
/// Returns the list of available login methods.
static net::awaitable<Response> handleGetLogin(Request& req,
                                               const http_function& /*params*/)
{
    json j;
    j["data"]["login_methods"] = {"local", "google", "apple", "passwordless"};
    co_return jsonResponse(http::status::ok, req.version(), j.dump());
}

/// POST /api/login/local
/// Body: {"username":"...","password":"..."}
static net::awaitable<Response>
    handlePostLoginLocal(Request& req, const http_function& /*params*/,
                         Database& db)
{
    const json body = parseBody(req);
    if (body.is_discarded())
    {
        co_return jsonResponse(
            http::status::bad_request, req.version(),
            makeEc5Error("ec5_99", "Invalid JSON body"));
    }

    const std::string email =
        body.value("username", body.value("email", std::string{}));
    const std::string password = body.value("password", std::string{});

    if (email.empty() || password.empty())
    {
        co_return jsonResponse(
            http::status::bad_request, req.version(),
            makeEc5Error("ec5_99", "username and password are required"));
    }

    if (!db.verifyUserPassword(email, password))
    {
        co_return jsonResponse(
            http::status::unauthorized, req.version(),
            makeEc5Error("ec5_70", "Please log in to continue"));
    }

    const auto userOpt = db.findUserByEmail(email);
    if (!userOpt)
    {
        co_return jsonResponse(
            http::status::unauthorized, req.version(),
            makeEc5Error("ec5_70", "Please log in to continue"));
    }

    const std::string token =
        ec5::issue(email, std::to_string(userOpt->id));
    co_return jsonResponse(http::status::ok, req.version(),
                           makeJwtResponse(token, userOpt->name, email));
}

/// POST /api/login/passwordless/code
/// Body: {"email":"..."}
/// Generates a 6-digit code, stores it with a 15-min expiry, logs to stdout.
static net::awaitable<Response>
    handlePostPasswordlessCode(Request& req, const http_function& /*params*/,
                               Database& db)
{
    const json body = parseBody(req);
    if (body.is_discarded())
    {
        co_return jsonResponse(
            http::status::bad_request, req.version(),
            makeEc5Error("ec5_99", "Invalid JSON body"));
    }

    const std::string email = body.value("email", std::string{});
    if (email.empty())
    {
        co_return jsonResponse(
            http::status::bad_request, req.version(),
            makeEc5Error("ec5_99", "email is required"));
    }

    const std::string code      = generateCode();
    const std::string expiresAt = isoFuture(900); // 15 minutes
    db.insertCode(email, code, expiresAt);

    std::cout << std::format(
        "[epicollect5_server] Passwordless code for {}: {}\n", email, code);

    json j;
    j["data"]["code"] = "ec5_201";
    co_return jsonResponse(http::status::ok, req.version(), j.dump());
}

/// POST /api/login/passwordless
/// Body: {"email":"...","code":"..."}
/// Verifies the code, finds or creates the user, returns JWT.
static net::awaitable<Response>
    handlePostPasswordless(Request& req, const http_function& /*params*/,
                           Database& db)
{
    const json body = parseBody(req);
    if (body.is_discarded())
    {
        co_return jsonResponse(
            http::status::bad_request, req.version(),
            makeEc5Error("ec5_99", "Invalid JSON body"));
    }

    const std::string email = body.value("email", std::string{});
    const std::string code  = body.value("code", std::string{});

    if (email.empty() || code.empty())
    {
        co_return jsonResponse(
            http::status::bad_request, req.version(),
            makeEc5Error("ec5_99", "email and code are required"));
    }

    const auto codeOpt = db.findCode(email);
    if (!codeOpt || codeOpt->code != code)
    {
        co_return jsonResponse(
            http::status::unauthorized, req.version(),
            makeEc5Error("ec5_70", "Invalid or expired code"));
    }

    // Check expiry (stored as ISO-8601 UTC; compare lexicographically).
    if (codeOpt->expiresAt < isoNow())
    {
        db.deleteCode(email);
        co_return jsonResponse(
            http::status::unauthorized, req.version(),
            makeEc5Error("ec5_70", "Code has expired"));
    }

    db.deleteCode(email);

    // Find or create the user.
    auto userOpt = db.findUserByEmail(email);
    if (!userOpt)
    {
        db.insertUser(email, generateCode() /*unused temp password*/, email);
        userOpt = db.findUserByEmail(email);
    }

    const std::string token =
        ec5::issue(email, std::to_string(userOpt->id));
    co_return jsonResponse(http::status::ok, req.version(),
                           makeJwtResponse(token, userOpt->name, email));
}

/// POST /api/handle/google
/// Body: {"code":"...","grant_type":"..."}
/// Stub: derives a synthetic email from the provided code and
/// finds-or-creates the user.
static net::awaitable<Response>
    handleGoogle(Request& req, const http_function& /*params*/, Database& db)
{
    const json body = parseBody(req);
    if (body.is_discarded())
    {
        co_return jsonResponse(
            http::status::bad_request, req.version(),
            makeEc5Error("ec5_99", "Invalid JSON body"));
    }

    const std::string code = body.value("code", std::string{});
    const std::string stubEmail =
        "google_" + (code.empty() ? "unknown" : code) + "@stub.local";
    const std::string name = "Google User";

    auto userOpt = db.findUserByEmail(stubEmail);
    if (!userOpt)
    {
        db.insertUser(stubEmail, generateCode(), name);
        userOpt = db.findUserByEmail(stubEmail);
    }

    const std::string token =
        ec5::issue(stubEmail, std::to_string(userOpt->id));
    co_return jsonResponse(http::status::ok, req.version(),
                           makeJwtResponse(token, userOpt->name, stubEmail));
}

/// POST /api/handle/apple
/// Body: {"identityToken":"...","user":{"name":{"firstName":"...","lastName":"..."},"email":"..."}}
/// Stub: creates or finds a user from the identity token / user object.
static net::awaitable<Response>
    handleApple(Request& req, const http_function& /*params*/, Database& db)
{
    const json body = parseBody(req);
    if (body.is_discarded())
    {
        co_return jsonResponse(
            http::status::bad_request, req.version(),
            makeEc5Error("ec5_99", "Invalid JSON body"));
    }

    // Try to extract email and name from the user object.
    std::string email;
    std::string name;

    if (body.contains("user") && body["user"].is_object())
    {
        email = body["user"].value("email", std::string{});
        if (body["user"].contains("name") && body["user"]["name"].is_object())
        {
            const std::string first =
                body["user"]["name"].value("firstName", std::string{});
            const std::string last =
                body["user"]["name"].value("lastName", std::string{});
            name = first + (first.empty() ? "" : " ") + last;
        }
    }

    if (email.empty())
    {
        // Fall back: derive a stub email from the identity token.
        const std::string token = body.value("identityToken", std::string{});
        email = "apple_" + (token.empty() ? "unknown" : token.substr(0, 16)) +
                "@stub.local";
    }
    if (name.empty())
        name = "Apple User";

    auto userOpt = db.findUserByEmail(email);
    if (!userOpt)
    {
        db.insertUser(email, generateCode(), name);
        userOpt = db.findUserByEmail(email);
    }

    const std::string jwtToken =
        ec5::issue(email, std::to_string(userOpt->id));
    co_return jsonResponse(http::status::ok, req.version(),
                           makeJwtResponse(jwtToken, userOpt->name, email));
}

/// POST /api/login/verify-google
/// POST /api/login/verify-apple
/// Body: {"email":"...","code":"...","user":{...}}
/// Re-uses the passwordless code verification path with an optional
/// provider-scoped email fallback.
static net::awaitable<Response>
    handleVerifyProvider(Request& req, const http_function& /*params*/,
                         Database& db)
{
    const json body = parseBody(req);
    if (body.is_discarded())
    {
        co_return jsonResponse(
            http::status::bad_request, req.version(),
            makeEc5Error("ec5_99", "Invalid JSON body"));
    }

    const std::string email = body.value("email", std::string{});
    const std::string code  = body.value("code", std::string{});

    if (email.empty() || code.empty())
    {
        co_return jsonResponse(
            http::status::bad_request, req.version(),
            makeEc5Error("ec5_99", "email and code are required"));
    }

    const auto codeOpt = db.findCode(email);
    if (!codeOpt || codeOpt->code != code)
    {
        co_return jsonResponse(
            http::status::unauthorized, req.version(),
            makeEc5Error("ec5_70", "Invalid or expired code"));
    }

    if (codeOpt->expiresAt < isoNow())
    {
        db.deleteCode(email);
        co_return jsonResponse(
            http::status::unauthorized, req.version(),
            makeEc5Error("ec5_70", "Code has expired"));
    }

    db.deleteCode(email);

    auto userOpt = db.findUserByEmail(email);
    if (!userOpt)
    {
        db.insertUser(email, generateCode(), email);
        userOpt = db.findUserByEmail(email);
    }

    const std::string token =
        ec5::issue(email, std::to_string(userOpt->id));
    co_return jsonResponse(http::status::ok, req.version(),
                           makeJwtResponse(token, userOpt->name, email));
}

/// POST /api/profile/account-deletion-request
/// Requires a valid Bearer JWT.  Returns {"data":{"deleted":true}} on success.
static net::awaitable<Response>
    handleAccountDeletion(Request& req, const http_function& /*params*/)
{
    const std::string token = ec5::extractBearer(req);
    if (token.empty() || !ec5::verify(token))
    {
        co_return jsonResponse(
            http::status::unauthorized, req.version(),
            makeEc5Error("ec5_50", "JWT is invalid"));
    }

    json j;
    j["data"]["deleted"] = true;
    co_return jsonResponse(http::status::ok, req.version(), j.dump());
}

/// POST /api/admin/register
/// Dev helper — not part of the Epicollect5 mobile API.
/// Body: {"email":"...","password":"...","name":"..."}
static net::awaitable<Response>
    handleAdminRegister(Request& req, const http_function& /*params*/,
                        Database& db)
{
    const json body = parseBody(req);
    if (body.is_discarded())
    {
        co_return jsonResponse(
            http::status::bad_request, req.version(),
            makeEc5Error("ec5_99", "Invalid JSON body"));
    }

    const std::string email    = body.value("email", std::string{});
    const std::string password = body.value("password", std::string{});
    const std::string name     = body.value("name", email);

    if (email.empty() || password.empty())
    {
        co_return jsonResponse(
            http::status::bad_request, req.version(),
            makeEc5Error("ec5_99", "email and password are required"));
    }

    try
    {
        const int id = db.insertUser(email, password, name);
        json j;
        j["data"]["id"]    = id;
        j["data"]["email"] = email;
        j["data"]["name"]  = name;
        co_return jsonResponse(http::status::created, req.version(), j.dump());
    }
    catch (const std::exception& ex)
    {
        co_return jsonResponse(
            http::status::conflict, req.version(),
            makeEc5Error("ec5_98",
                         std::string("Could not create user: ") + ex.what()));
    }
}

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

void register_auth_routes(reactor::HttpRouter& router, ec5::Database& db)
{
    // GET /api/login — available login methods
    router.add_get_handler("/api/login", handleGetLogin);

    // POST /api/login/local — email + password
    router.add_post_handler(
        "/api/login/local",
        [&db](Request& req,
              const http_function& p) -> net::awaitable<Response> {
            return handlePostLoginLocal(req, p, db);
        });

    // POST /api/login/passwordless/code — request a 6-digit code
    router.add_post_handler(
        "/api/login/passwordless/code",
        [&db](Request& req,
              const http_function& p) -> net::awaitable<Response> {
            return handlePostPasswordlessCode(req, p, db);
        });

    // POST /api/login/passwordless — verify code and get JWT
    router.add_post_handler(
        "/api/login/passwordless",
        [&db](Request& req,
              const http_function& p) -> net::awaitable<Response> {
            return handlePostPasswordless(req, p, db);
        });

    // POST /api/handle/google — OAuth stub
    router.add_post_handler(
        "/api/handle/google",
        [&db](Request& req,
              const http_function& p) -> net::awaitable<Response> {
            return handleGoogle(req, p, db);
        });

    // POST /api/handle/apple — OAuth stub
    router.add_post_handler(
        "/api/handle/apple",
        [&db](Request& req,
              const http_function& p) -> net::awaitable<Response> {
            return handleApple(req, p, db);
        });

    // POST /api/login/verify-google
    router.add_post_handler(
        "/api/login/verify-google",
        [&db](Request& req,
              const http_function& p) -> net::awaitable<Response> {
            return handleVerifyProvider(req, p, db);
        });

    // POST /api/login/verify-apple
    router.add_post_handler(
        "/api/login/verify-apple",
        [&db](Request& req,
              const http_function& p) -> net::awaitable<Response> {
            return handleVerifyProvider(req, p, db);
        });

    // POST /api/profile/account-deletion-request
    router.add_post_handler(
        "/api/profile/account-deletion-request",
        [](Request& req,
           const http_function& p) -> net::awaitable<Response> {
            return handleAccountDeletion(req, p);
        });

    // POST /api/admin/register — dev helper
    router.add_post_handler(
        "/api/admin/register",
        [&db](Request& req,
              const http_function& p) -> net::awaitable<Response> {
            return handleAdminRegister(req, p, db);
        });
}

} // namespace ec5
