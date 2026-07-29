#pragma once
/**
 * async_component_integrity.hpp — async D-Bus ComponentIntegrity object.
 *
 * All SPDM protocol operations (measurements, certificate exchange,
 * provisioning) are driven via co_await using the new async SPDM helpers:
 *   - libspdm_get_digest_async
 *   - libspdm_get_certificate_async
 *   - libspdm_get_measurement_async
 *   - asyncPushCertificate / asyncPullCertificate / asyncSetProvisioned
 *
 * No thread pool is used.  The io_context is never blocked.
 */

#include "async_spdm_requester.hpp"
#include "async_wait.hpp"
#include "dbusproperty_watcher.hpp"
#include "logger.hpp"
#include "sdbus_calls.hpp"

#include "async/libspdm_req_custom_messages_async.hpp"
#include "async/libspdm_req_get_certificate_async.hpp"
#include "async/libspdm_req_get_digest_async.hpp"
#include "async/libspdm_req_get_measurement_async.hpp"

#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/bus.hpp>
#include <xyz/openbmc_project/Attestation/ComponentIntegrity/aserver_asio.hpp>
#include <xyz/openbmc_project/Attestation/IdentityAuthentication/aserver_asio.hpp>
#include <xyz/openbmc_project/Attestation/MeasurementSet/aserver_asio.hpp>
#include <xyz/openbmc_project/Attestation/SecureExchange/aserver_asio.hpp>

extern "C"
{
#include <internal/libspdm_common_lib.h>
}

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <variant>

namespace net = boost::asio;
using namespace reactor;
using namespace std::chrono_literals;

namespace spdm_async
{

template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

class AsyncComponentIntegrity;

using ComponentIntegrityIface =
    sdbusplus::asio::aserver::xyz::openbmc_project::attestation::
        ComponentIntegrity<AsyncComponentIntegrity, void>;
using IdentityAuthenticationIface =
    sdbusplus::asio::aserver::xyz::openbmc_project::attestation::
        IdentityAuthentication<AsyncComponentIntegrity, void>;
using MeasurementSetIface =
    sdbusplus::asio::aserver::xyz::openbmc_project::attestation::MeasurementSet<
        AsyncComponentIntegrity, void>;
using SecureExchangeIface =
    sdbusplus::asio::aserver::xyz::openbmc_project::attestation::SecureExchange<
        AsyncComponentIntegrity, void>;

static constexpr auto CompIntegrityPath =
    "/xyz/openbmc_project/ComponentIntegrity/{}";

class AsyncComponentIntegrity :
    public ComponentIntegrityIface,
    public IdentityAuthenticationIface,
    public MeasurementSetIface,
    public SecureExchangeIface
{
  public:
    using MeasurementResult =
        std::tuple<sdbuscompat::object_path, std::string, std::string,
                   std::string, std::string, std::string>;

    struct TcpDeviceInfo
    {
        std::string host;
        int port;
    };

    struct DeviceInfo
    {
        std::string id() const
        {
            return std::visit(
                overloaded{
                    [](std::monostate) -> std::string { return "null"; },
                    [](const TcpDeviceInfo& info) -> std::string {
                        std::string ip = info.host;
                        std::replace(ip.begin(), ip.end(), '.', '_');
                        return ip + "_" + std::to_string(info.port);
                    },
                },
                info);
        }
        std::variant<std::monostate, TcpDeviceInfo> info;
    };

    AsyncComponentIntegrity(net::io_context& ctx,
                            std::shared_ptr<sdbusplus::asio::connection> conn,
                            std::shared_ptr<AsyncSpdmRequester> requester,
                            DeviceInfo deviceInfo = DeviceInfo{}) :
        ComponentIntegrityIface(
            *conn, ctx,
            std::format(CompIntegrityPath, deviceInfo.id()).c_str()),
        IdentityAuthenticationIface(
            *conn, ctx,
            std::format(CompIntegrityPath, deviceInfo.id()).c_str()),
        MeasurementSetIface(
            *conn, ctx,
            std::format(CompIntegrityPath, deviceInfo.id()).c_str()),
        SecureExchangeIface(
            *conn, ctx,
            std::format(CompIntegrityPath, deviceInfo.id()).c_str()),
        ioContext_(ctx), conn_(conn), requester_(std::move(requester)),
        deviceInfo_(std::move(deviceInfo))
    {}

    net::io_context& getContextRef() noexcept { return ioContext_; }

    // ── ComponentIntegrity properties ─────────────────────────────────────
    bool enabled() const { return true; }
    bool enabled(bool v, bool) { return v; }

    ComponentIntegrityIface::SecurityTechnologyType type() const
    {
        return ComponentIntegrityIface::SecurityTechnologyType::SPDM;
    }
    ComponentIntegrityIface::SecurityTechnologyType type(
        ComponentIntegrityIface::SecurityTechnologyType v, bool)
    {
        return v;
    }

    std::string typeVersion() const { return "1.1"; }
    std::string typeVersion(std::string v, bool) { return v; }

    uint64_t lastUpdated() const { return 0; }
    uint64_t lastUpdated(uint64_t v, bool) { return v; }

    // ── MeasurementSet::spdmGetSignedMeasurements ─────────────────────────
    net::awaitable<MeasurementResult> method_call(
        MeasurementSetIface::spdm_get_signed_measurements_t,
        std::vector<size_t> measurementIndices, std::string /*nonce*/,
        size_t slotId)
    {
        auto& io = requester_->getIO();
        void* ctx = requester_->getSpdmContext();

        // Step 1: GET_DIGESTS
        constexpr size_t kMaxDigestBuf = 8 * 48; // 8 slots × 48-byte SHA384
        std::vector<uint8_t> digestBuf(kMaxDigestBuf, 0);
        uint8_t slotMask = 0;
        auto status = co_await libspdm_get_digest_async(
            ctx, nullptr, &slotMask, digestBuf.data(), io);
        if (LIBSPDM_STATUS_IS_ERROR(status))
        {
            LOG_ERROR("getSignedMeasurements: GET_DIGESTS failed 0x{:x}",
                      static_cast<uint32_t>(status));
            throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
        }

        // Step 2: GET_CERTIFICATE
        std::vector<uint8_t> certChain;
        status = co_await libspdm_get_certificate_async(
            ctx, nullptr, static_cast<uint8_t>(slotId), certChain, io);
        if (LIBSPDM_STATUS_IS_ERROR(status))
        {
            LOG_ERROR("getSignedMeasurements: GET_CERTIFICATE failed 0x{:x}",
                      static_cast<uint32_t>(status));
            throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
        }

        // Step 3: GET_MEASUREMENTS for each requested index
        std::vector<uint8_t> allMeasurements;
        auto indicesToProcess = measurementIndices.empty()
                                    ? std::vector<size_t>{255}
                                    : measurementIndices;

        for (size_t idx : indicesToProcess)
        {
            std::vector<uint8_t> meas;
            uint8_t numBlocks = 0;
            status = co_await libspdm_get_measurement_async(
                ctx, nullptr,
                0, // no signature requested
                static_cast<uint8_t>(idx),
                static_cast<uint8_t>(slotId), &numBlocks, meas, io);
            if (LIBSPDM_STATUS_IS_ERROR(status))
            {
                LOG_ERROR(
                    "getSignedMeasurements: GET_MEASUREMENTS[{}] failed 0x{:x}",
                    idx, static_cast<uint32_t>(status));
                throw sdbusplus::xyz::openbmc_project::Common::Error::
                    Unavailable();
            }
            allMeasurements.insert(allMeasurements.end(), meas.begin(),
                                   meas.end());
        }

        // Encode measurements to base64
        std::string measBase64 = encodeBase64(allMeasurements);

        // Extract PEM chain and algorithm strings from context
        auto* spdmCtx = static_cast<libspdm_context_t*>(ctx);
        std::string hashAlgo = getHashingAlgorithmStr(
            spdmCtx->connection_info.algorithm.base_hash_algo);
        std::string signAlgo = getSigningAlgorithmStr(
            spdmCtx->connection_info.algorithm.base_asym_algo);
        std::string certPem = derChainToPem(certChain);

        co_return MeasurementResult{sdbuscompat::object_path("/some/path"),
                                    hashAlgo, certPem, measBase64, signAlgo,
                                    "1.1"};
    }

    // ── MeasurementSet::exchangeCertificate ───────────────────────────────
    net::awaitable<std::tuple<bool, std::string>> method_call(
        MeasurementSetIface::exchange_certificate_t)
    {
        auto& io = requester_->getIO();

        // Load requester cert from disk
        auto certBytes = loadPemFile("/etc/ssl/certs/self_ca.pem");
        if (certBytes.empty())
        {
            LOG_ERROR("exchangeCertificate: failed to load requester cert");
            throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
        }

        // Push requester cert to responder
        void* ctx = requester_->getSpdmContext();
        bool ok = co_await asyncPushCertificate(certBytes,
                                                CertificateFormat::PEM, ctx, io);
        if (!ok)
        {
            LOG_ERROR("exchangeCertificate: PUSH_CERTIFICATE failed");
            throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
        }

        // Pull responder cert
        std::vector<uint8_t> responderCert;
        ok = co_await asyncPullCertificate(CertificateFormat::PEM,
                                           responderCert, ctx, io);
        if (!ok)
        {
            LOG_ERROR("exchangeCertificate: PULL_CERTIFICATE failed");
            throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
        }

        std::string certPath = storeCert(responderCert,
                                         "/etc/ssl/certs/authority",
                                         "responder_async");
        LOG_INFO("exchangeCertificate: stored responder cert at {}", certPath);
        co_return std::make_tuple(true, certPath);
    }

    // ── MeasurementSet::setProvisioned ────────────────────────────────────
    net::awaitable<bool> method_call(MeasurementSetIface::set_provisioned_t,
                                     bool provisioned)
    {
        auto& io = requester_->getIO();
        void* ctx = requester_->getSpdmContext();
        bool ok = co_await asyncSetProvisioned(provisioned, ctx, io);
        if (!ok)
        {
            LOG_ERROR("setProvisioned: SET_PROVISIONED({}) failed", provisioned);
            throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
        }
        co_return true;
    }

    // ── SecureExchange::exchangeAppData ───────────────────────────────────
    net::awaitable<void> method_call(SecureExchangeIface::exchange_app_data_t)
    {
        // Reuse the exchange_certificate + set_provisioned flow
        co_await method_call(MeasurementSetIface::exchange_certificate_t{});
        co_await method_call(MeasurementSetIface::set_provisioned_t{}, true);
        co_return;
    }

    // ── Static factory ─────────────────────────────────────────────────────
    static void addComponentIntegrity(
        net::io_context& ioContext,
        std::shared_ptr<sdbusplus::asio::connection> conn,
        DeviceInfo deviceInfo, int retry = 0)
    {
        // Guard: don't spawn a second coroutine if one is already live for
        // this device (either connected or mid-retry).
        std::string path = std::format(CompIntegrityPath, deviceInfo.id());
        if (spdmDevices.count(path))
        {
            LOG_DEBUG("AsyncComponentIntegrity: already active for {}, skipping",
                      deviceInfo.id());
            return;
        }

        // Insert a nullptr sentinel so re-entrant calls see the device as
        // "in progress" and return immediately.
        spdmDevices[path] = nullptr;

        net::co_spawn(
            ioContext,
            [&ioContext, conn, deviceInfo = std::move(deviceInfo),
             retry, path]() -> net::awaitable<void> {
                std::string host;
                int port = 0;
                if (std::holds_alternative<TcpDeviceInfo>(deviceInfo.info))
                {
                    const auto& tcp = std::get<TcpDeviceInfo>(deviceInfo.info);
                    host = tcp.host;
                    port = tcp.port;
                }

                // Exponential back-off: 5s, 10s, 20s, … capped at 60s.
                auto delay = std::chrono::seconds(
                    std::min(5 * (1 << retry), 60));

                if (retry > 0)
                {
                    LOG_INFO("AsyncComponentIntegrity: retry {} for {}:{} "
                             "in {}s",
                             retry, host, port, delay.count());
                    co_await reactor::waitFor(ioContext.get_executor(), delay);
                }

                LOG_INFO("Async SPDM connect to {}:{}", host, port);

                auto requester =
                    std::make_shared<AsyncSpdmRequester>(ioContext);

                bool ok = co_await requester->connectAndInit(
                    host, static_cast<uint16_t>(port));
                if (!ok)
                {
                    LOG_ERROR(
                        "AsyncComponentIntegrity: connect/init failed {}:{}",
                        host, port);
                    // Remove sentinel so the next retry can re-insert it.
                    spdmDevices.erase(path);
                    // Re-schedule with incremented retry counter (no upper
                    // bound — keeps retrying with increasing back-off).
                    addComponentIntegrity(ioContext, conn, deviceInfo,
                                          retry + 1);
                    co_return;
                }

                LOG_INFO("AsyncComponentIntegrity: SPDM negotiation complete "
                         "for {}",
                         deviceInfo.id());

                auto obj = std::make_shared<AsyncComponentIntegrity>(
                    ioContext, conn, std::move(requester), deviceInfo);

                spdmDevices[path] = std::move(obj);
            },
            net::detached);
    }

    static void removeComponentIntegrity(const std::string& deviceId)
    {
        std::string path = std::format(CompIntegrityPath, deviceId);
        auto it = spdmDevices.find(path);
        if (it != spdmDevices.end())
        {
            LOG_INFO("AsyncComponentIntegrity: removing {}", path);
            spdmDevices.erase(it);
        }
    }

    static std::map<std::string, std::shared_ptr<AsyncComponentIntegrity>>
        spdmDevices;

  private:
    // ── Helpers ───────────────────────────────────────────────────────────

    static std::string encodeBase64(const std::vector<uint8_t>& data)
    {
        static constexpr char kTable[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        size_t i = 0;
        for (; i + 2 < data.size(); i += 3)
        {
            uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
            out += kTable[(n >> 18) & 63];
            out += kTable[(n >> 12) & 63];
            out += kTable[(n >> 6) & 63];
            out += kTable[n & 63];
        }
        if (i < data.size())
        {
            uint32_t n = data[i] << 16;
            out += kTable[(n >> 18) & 63];
            if (i + 1 < data.size())
            {
                n |= data[i + 1] << 8;
                out += kTable[(n >> 12) & 63];
                out += kTable[(n >> 6) & 63];
                out += '=';
            }
            else
            {
                out += kTable[(n >> 12) & 63];
                out += "==";
            }
        }
        return out;
    }

    static std::string derChainToPem(const std::vector<uint8_t>& chain)
    {
        // Skip the 4-byte SPDM cert-chain header + hash
        // The caller receives the raw chain from libspdm; the first
        // 4 bytes are Length(2)+Reserved(2) followed by the root hash.
        // Use libspdm_x509_get_cert_from_cert_chain to iterate certs.
        std::string pem;
        size_t offset = 0;
        size_t idx = 0;
        while (offset < chain.size())
        {
            const uint8_t* certPtr = nullptr;
            size_t certLen = 0;
            if (!libspdm_x509_get_cert_from_cert_chain(
                    chain.data(), chain.size(), idx, &certPtr, &certLen))
                break;
            std::string b64 = encodeBase64(
                std::vector<uint8_t>(certPtr, certPtr + certLen));
            pem += "-----BEGIN CERTIFICATE-----\n";
            for (size_t j = 0; j < b64.size(); j += 64)
                pem += b64.substr(j, 64) + "\n";
            pem += "-----END CERTIFICATE-----\n";
            offset += certLen;
            ++idx;
        }
        return pem;
    }

    static std::vector<uint8_t> loadPemFile(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return {};
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                    std::istreambuf_iterator<char>());
    }

    static std::string storeCert(const std::vector<uint8_t>& cert,
                                  const std::string& dir,
                                  const std::string& name)
    {
        std::string path = dir + "/" + name + ".pem";
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (f)
            f.write(reinterpret_cast<const char*>(cert.data()), cert.size());
        return path;
    }

    net::io_context& ioContext_;
    std::shared_ptr<sdbusplus::asio::connection> conn_;
    std::shared_ptr<AsyncSpdmRequester> requester_;
    DeviceInfo deviceInfo_;
};

} // namespace spdm_async
