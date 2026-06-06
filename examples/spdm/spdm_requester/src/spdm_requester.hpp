#pragma once
#include "logger.hpp"
#include "spdm_certificate_client.hpp"
#include "spdm_connection.hpp"
#include "spdm_measurement_client.hpp"
#include "spdmglobal.hpp"

extern "C"
{
#include <internal/libspdm_common_lib.h>
}

#include <boost/asio/io_context.hpp>

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

using GetMeasurementsReturnType =
    std::tuple<std::string, std::string, std::string, std::string, std::string>;

// Facade class that composes the three specialized clients
struct SpdmRequester
{
  public:
    SpdmRequester(boost::asio::io_context& io,
                  const std::string& trustStorePath) :
        connection_(std::make_unique<SpdmConnectionManager>(io)),
        certificateClient_(nullptr), measurementClient_(nullptr)
    {
        // Initialize certificate and measurement clients with the connection's
        // context
        void* spdmContext = connection_->getSpdmContext();
        certificateClient_ = std::make_unique<SpdmCertificateClient>(
            spdmContext, trustStorePath);
        measurementClient_ =
            std::make_unique<SpdmMeasurementClient>(spdmContext);

        // Set send/receive callback functions for certificate exchange
        certificateClient_->setSendFunction(
            &SpdmConnectionManager::device_send_message);
        certificateClient_->setReceiveFunction(
            &SpdmConnectionManager::device_receive_message_all);
    }

    ~SpdmRequester() = default;

    // Delegate connection methods to SpdmConnection
    bool connect(const std::string& host, uint16_t port)
    {
        return connection_->connect(host, port);
    }

    bool sayhello()
    {
        return connection_->sayhello();
    }

    bool init_connection()
    {
        return connection_->init_connection();
    }

    bool setProvisioned(bool provisioned)
    {
        return connection_->setProvisioned(provisioned);
    }

    std::string type_version()
    {
        return connection_->type_version();
    }

    // Delegate certificate methods to SpdmCertificateClient
    std::tuple<bool, std::string> exchangeCertificates(
        const std::string& requesterCertPath =
            "/var/lib/spdm/certs/requester_cert.der")
    {
        return certificateClient_->exchangeCertificates(requesterCertPath);
    }

    CertificateExchangeRequester* getCertExchangeRequester()
    {
        return certificateClient_->getCertExchangeRequester();
    }

    // Delegate measurement methods to SpdmMeasurementClient
    std::optional<GetMeasurementsReturnType> getSignedMeasurements(
        const std::vector<size_t>& measurementIndices [[maybe_unused]],
        const std::string& nonce, size_t slotId [[maybe_unused]])
    {
        auto [success, data] = certificateClient_->getCertificateDigests();
        auto [slotMask, digestBuffer, totalDigestSize] = data;
        auto [success1, data1] = certificateClient_->getCertificate(slotId);
        auto [certPem, certRaw, certLeaf] = data1;

        // Get signed measurements using measurement client
        auto signedMeas = measurementClient_->getSignedMeasurementsImpl(
            measurementIndices, nonce, slotId);
        if (!signedMeas)
        {
            return std::nullopt;
        }

        auto* spdmCtx =
            reinterpret_cast<libspdm_context_t*>(connection_->getSpdmContext());
        auto hashAlgoStr = getHashingAlgorithmStr(
            spdmCtx->connection_info.algorithm.base_hash_algo);
        auto signAlgoStr = getSigningAlgorithmStr(
            spdmCtx->connection_info.algorithm.base_asym_algo);
        auto versionStr = type_version();

        return std::make_tuple(hashAlgoStr, certPem, *signedMeas, signAlgoStr,
                               versionStr);
    }

  private:
    std::unique_ptr<SpdmConnectionManager> connection_;
    std::unique_ptr<SpdmCertificateClient> certificateClient_;
    std::unique_ptr<SpdmMeasurementClient> measurementClient_;
};
