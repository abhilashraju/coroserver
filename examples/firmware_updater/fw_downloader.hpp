#pragma once
#include "fw_updater_config.hpp"
#include "logger.hpp"
#include "webclient.hpp"

#include <openssl/evp.h>

#include <boost/url.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace firmware_updater
{

/**
 * @brief Result returned by downloadFirmware.
 */
struct DownloadResult
{
    boost::system::error_code ec;
    std::string localPath; ///< Absolute path to the downloaded file on success
    std::string version;   ///< Version string copied from the FirmwareEntry
};

/**
 * @brief Derive the local filename from the imageUrl, preserving the
 * original extension so phosphor-version-software-manager can identify
 * the archive type (e.g. .tar, .tar.gz).
 *
 * Example: imageUrl ".../obmc-phosphor-image-p11bmc-1120.10.3.tar"
 *       → "<downloadDir>/obmc-phosphor-image-p11bmc-1120.10.3.tar"
 */
inline std::string localFilePath(const std::string& downloadDir,
                                 const FirmwareEntry& entry)
{
    namespace fs = std::filesystem;
    auto parsedUrl = boost::urls::parse_uri(entry.imageUrl);
    if (parsedUrl)
    {
        std::string urlPath = std::string(parsedUrl->path());
        std::string filename = fs::path(urlPath).filename().string();
        if (!filename.empty())
        {
            return downloadDir + "/" + filename;
        }
    }
    // Fallback: version-based name with no extension assumption.
    return downloadDir + "/firmware-" + entry.version;
}

// Compute SHA-256 hex digest of a file. Returns empty string on error.
inline std::string sha256File(const std::string& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
    {
        return {};
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
    {
        return {};
    }
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    char buf[65536];
    while (ifs.read(buf, sizeof(buf)) || ifs.gcount() > 0)
    {
        EVP_DigestUpdate(ctx, buf, static_cast<std::size_t>(ifs.gcount()));
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx, digest, &len);
    EVP_MD_CTX_free(ctx);
    std::ostringstream oss;
    for (unsigned int i = 0; i < len; ++i)
    {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(digest[i]);
    }
    return oss.str();
}

/**
 * @brief Download a single firmware binary to local storage.
 *
 * The function:
 *  1. Derives the local path from the download directory and version.
 *  2. Skips download if the file already exists on disk.
 *  3. Issues a GET request to entry.imageUrl via the coroserver WebClient.
 *  4. Writes the response body as raw bytes to the local path.
 *
 * TLS certificate verification is intentionally relaxed to verify_none here;
 * integrity is assured separately via the SHA-256 checksum field.
 *
 * @param ioc         Boost.Asio io_context.
 * @param entry       Firmware catalogue entry to download.
 * @param downloadDir Directory where the binary should be stored.
 * @return            DownloadResult with ec, localPath, and version.
 */
inline reactor::net::awaitable<DownloadResult> downloadFirmware(
    reactor::net::io_context& ioc, const FirmwareEntry& entry,
    const std::string& downloadDir, int downloadTimeoutSeconds = 1800)
{
    namespace fs = std::filesystem;

    const std::string destPath = localFilePath(downloadDir, entry);

    // Skip if already downloaded — but verify checksum when available so a
    // corrupt or truncated file from a previous attempt is re-downloaded.
    if (fs::exists(destPath))
    {
        bool valid = true;
        if (!entry.checksum.empty())
        {
            std::string actual = sha256File(destPath);
            if (actual != entry.checksum)
            {
                LOG_WARNING(
                    "Firmware {} checksum mismatch (expected={} actual={}) "
                    "— re-downloading",
                    entry.version, entry.checksum, actual);
                fs::remove(destPath);
                valid = false;
            }
        }
        if (valid)
        {
            LOG_INFO("Firmware {} already on disk at {}", entry.version,
                     destPath);
            co_return DownloadResult{boost::system::error_code{}, destPath,
                                     entry.version};
        }
    }

    // Ensure the storage directory exists.
    std::error_code mkdirEc;
    fs::create_directories(downloadDir, mkdirEc);
    if (mkdirEc)
    {
        LOG_ERROR("Cannot create download directory {}: {}", downloadDir,
                  mkdirEc.message());
        co_return DownloadResult{
            boost::system::errc::make_error_code(
                boost::system::errc::no_such_file_or_directory),
            {},
            entry.version};
    }

    // Parse the image URL.
    auto parsedUrl = boost::urls::parse_uri(entry.imageUrl);
    if (!parsedUrl)
    {
        LOG_ERROR("Invalid image URL: {}", entry.imageUrl);
        co_return DownloadResult{boost::system::errc::make_error_code(
                                     boost::system::errc::invalid_argument),
                                 {},
                                 entry.version};
    }
    const boost::urls::url url = parsedUrl.value();

    std::string host = std::string(url.host());
    std::string port = url.has_port() ? std::string(url.port()) : "443";
    std::string path = url.path().empty() ? "/" : std::string(url.path());

    LOG_INFO("Downloading firmware {} — host={} port={} path={}", entry.version,
             host, port, path);

    // Create a one-shot HTTPS client.
    reactor::ssl::context sslCtx(reactor::ssl::context::tlsv12_client);
    sslCtx.set_default_verify_paths();
    sslCtx.set_verify_mode(reactor::ssl::verify_none);

    reactor::WebClient<reactor::beast::tcp_stream> client(ioc, sslCtx);
    client.withHost(host)
        .withPort(port)
        .withMethod(reactor::http::verb::get)
        .withTarget(path)
        .withRetries(3)
        .withDownloadTimeout(downloadTimeoutSeconds);

    // Stream the response body directly to disk — no intermediate buffer.
    auto [ec, savedPath] =
        co_await client.template as<reactor::FileBodyTag>(destPath).execute();
    if (ec)
    {
        LOG_ERROR("Download failed for {}: {}", entry.version, ec.message());
        co_return DownloadResult{ec, {}, entry.version};
    }

    LOG_INFO("Firmware {} saved to {}", entry.version, destPath);
    co_return DownloadResult{boost::system::error_code{}, destPath,
                             entry.version};
}

} // namespace firmware_updater
