// Epicollect5-compatible server — Sub-Task 4: Authentication routes wired in.
// Starts an HTTPS server on port 8080 (TLS required by HttpServer).
// A self-signed certificate is generated in-memory at startup so no cert
// files are needed for local development.

#include "admin_handlers.hpp"
#include "auth_handlers.hpp"
#include "cors_middleware.hpp"
#include "db.hpp"
#include "entry_handlers.hpp"
#include "error_helpers.hpp"
#include "http_server.hpp"
#include "jwt_service.hpp"
#include "logger.hpp"
#include "media_handlers.hpp"
#include "project_handlers.hpp"
#include "utility_handlers.hpp"

#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace reactor;

// ---------------------------------------------------------------------------
// Generate an in-memory self-signed certificate and load it into ssl_context.
// Returns true on success.
// ---------------------------------------------------------------------------
static bool loadSelfSignedCert(ssl::context& ctx)
{
    // Generate RSA key
    EVP_PKEY* pkey = EVP_RSA_gen(2048);
    if (!pkey)
    {
        return false;
    }
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkeyGuard(
        pkey, EVP_PKEY_free);

    // Create X509 certificate
    X509* x509 = X509_new();
    if (!x509)
    {
        return false;
    }
    std::unique_ptr<X509, decltype(&X509_free)> x509Guard(x509, X509_free);

    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 60 * 60 * 24 * 365); // 1 year
    X509_set_pubkey(x509, pkey);

    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(
        name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0);
    X509_set_issuer_name(x509, name);
    X509_sign(x509, pkey, EVP_sha256());

    // Write cert and key into memory BIOs then load them
    BIO* certBio = BIO_new(BIO_s_mem());
    BIO* keyBio = BIO_new(BIO_s_mem());
    std::unique_ptr<BIO, decltype(&BIO_free)> certBioGuard(certBio, BIO_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> keyBioGuard(keyBio, BIO_free);

    PEM_write_bio_X509(certBio, x509);
    PEM_write_bio_PrivateKey(keyBio, pkey, nullptr, nullptr, 0, nullptr,
                             nullptr);

    BUF_MEM* certMem = nullptr;
    BUF_MEM* keyMem = nullptr;
    BIO_get_mem_ptr(certBio, &certMem);
    BIO_get_mem_ptr(keyBio, &keyMem);

    ctx.use_certificate(boost::asio::buffer(certMem->data, certMem->length),
                        ssl::context::pem);
    ctx.use_private_key(boost::asio::buffer(keyMem->data, keyMem->length),
                        ssl::context::pem);

    return true;
}

// ---------------------------------------------------------------------------
// OPTIONS preflight + catch-all fallback.
// Handles OPTIONS for every path (browser PWA preflight) and returns 404
// for any other unmatched route.
// ---------------------------------------------------------------------------
static net::awaitable<Response> catchAllHandler(Request& req,
                                                const http_function& /*params*/)
{
    // Handle OPTIONS preflight for any path
    if (req.method() == http::verb::options)
    {
        co_return ec5::makePreflightResponse(req.version());
    }

    // 404 for everything else
    co_return ec5::makeEc5Error("ec5_0", "Not found",
                                http::status::not_found, req.version());
}

// ---------------------------------------------------------------------------
// Health check handler — GET /api/health
// ---------------------------------------------------------------------------
static net::awaitable<Response> healthHandler(Request& req,
                                              const http_function& /*params*/)
{
    Response res{http::status::ok, req.version()};
    res.set(http::field::content_type, "application/json");
    ec5::addCorsHeaders(res);
    res.keep_alive(false);
    res.body() = R"({"status":"ok","service":"epicollect5-server"})";
    res.prepare_payload();
    co_return res;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    try
    {
        const short port = []() -> short {
            const char* env = std::getenv("EC5_PORT");
            return env ? static_cast<short>(std::stoi(env)) : 8080;
        }();

        const std::string dbPath = []() -> std::string {
            const char* env = std::getenv("EC5_DB_PATH");
            return env ? std::string(env) : "./ec5.db";
        }();

        // Open (or create) the SQLite database and run schema migrations.
        ec5::Database db;
        db.openOrCreate(dbPath);
        LOG_INFO("Database opened at {}", dbPath);
        std::cout << std::format("[epicollect5_server] Database: {}\n", dbPath);

        net::io_context ioc;

        ssl::context sslCtx(ssl::context::tlsv12_server);
        sslCtx.set_options(
            ssl::context::default_workarounds | ssl::context::no_sslv2 |
            ssl::context::single_dh_use);

        if (!loadSelfSignedCert(sslCtx))
        {
            throw std::runtime_error(
                "Failed to generate self-signed certificate");
        }

        HttpRouter router;
        router.setIoContext(std::ref(ioc));

        // Named routes
        router.add_get_handler("/api/health", healthHandler);

        // Admin HTML UI — /admin
        ec5::register_admin_routes(router, db);

        // Authentication routes (Sub-Task 4)
        ec5::register_auth_routes(router, db);

        // Project routes (Sub-Task 5)
        ec5::register_project_routes(router, db);

        // Entry upload/download routes (Sub-Task 6)
        ec5::register_entry_routes(router, db);

        // Media file routes (Sub-Task 7)
        ec5::register_media_routes(router, db);

        // Utility routes (Sub-Task 8)
        ec5::register_utility_routes(router, db);

        // Catch-all fallback
        router.set_fallback_handler(catchAllHandler);

        TcpStreamType acceptor(ioc.get_executor(), port, sslCtx);

        LOG_INFO("epicollect5_server listening on port {}", port);
        std::cout << std::format(
            "[epicollect5_server] Listening on https://localhost:{}\n", port);

        HttpServer server(ioc, acceptor, router);

        ioc.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
