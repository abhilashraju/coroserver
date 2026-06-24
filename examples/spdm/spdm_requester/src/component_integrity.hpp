#pragma once

#include "worker.hpp"

#include <async_wait.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/bus.hpp>
#include <xyz/openbmc_project/Attestation/ComponentIntegrity/aserver_asio.hpp>
#include <xyz/openbmc_project/Attestation/IdentityAuthentication/aserver_asio.hpp>
#include <xyz/openbmc_project/Attestation/MeasurementSet/aserver_asio.hpp>
#include <xyz/openbmc_project/Attestation/SecureExchange/aserver_asio.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>
namespace net = boost::asio;
using namespace reactor;
namespace spdm
{
// Helper for std::visit with multiple lambdas
template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
class ComponentIntegrity;
using ComponentIntegrityIface = sdbusplus::asio::aserver::xyz::openbmc_project::
    attestation::ComponentIntegrity<ComponentIntegrity, void>;
using IdentityAuthenticationIface =
    sdbusplus::asio::aserver::xyz::openbmc_project::attestation::
        IdentityAuthentication<ComponentIntegrity, void>;
using MeasurementSetIface =
    sdbusplus::asio::aserver::xyz::openbmc_project::attestation::MeasurementSet<
        ComponentIntegrity, void>;
using SecureExchangeIface =
    sdbusplus::asio::aserver::xyz::openbmc_project::attestation::SecureExchange<
        ComponentIntegrity, void>;

static constexpr auto componentIntegrityIface =
    "com.ibm.spdm.ComponentIntegrity";
static constexpr auto CompIntegrityPath =
    "/xyz/openbmc_project/ComponentIntegrity/{}";

class ComponentIntegrity :
    public ComponentIntegrityIface,
    public IdentityAuthenticationIface,
    public MeasurementSetIface,
    public SecureExchangeIface
{
  public:
    using MeasurementResult =
        std::tuple<sdbusplus::message::object_path, std::string, std::string,
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

  private:
    net::io_context& ioContext;
    std::shared_ptr<sdbusplus::asio::connection> conn;
    sdbusplus::asio::object_server server;
    std::shared_ptr<SpdmRequester> requester;
    DeviceInfo deviceInfo;

  public:
    ComponentIntegrity() = delete;
    ComponentIntegrity(const ComponentIntegrity&) = delete;
    ComponentIntegrity& operator=(const ComponentIntegrity&) = delete;
    ComponentIntegrity(ComponentIntegrity&&) = delete;
    ComponentIntegrity& operator=(ComponentIntegrity&&) = delete;

    /**
     * @brief Construct a new ComponentIntegrity from a bus reference
     * @param ctx Async context for D-Bus operations
     * @param path Object path for this component
     */
    ComponentIntegrity(net::io_context& ctx,
                       std::shared_ptr<sdbusplus::asio::connection> conn,
                       std::shared_ptr<SpdmRequester> requester,
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
        ioContext(ctx), conn(conn), server(conn),
        requester(std::move(requester)), deviceInfo(std::move(deviceInfo))
    {}

    // CRTP method to provide context reference to base classes
    boost::asio::io_context& getContextRef() noexcept
    {
        return ioContext;
    }
    /** Get value of Enabled */
    bool enabled() const
    {
        return true;
    }
    bool enabled(bool value, bool skipSignal)
    {
        return value;
    }

    /** Get value of Type */
    ComponentIntegrityIface::SecurityTechnologyType type() const
    {
        return ComponentIntegrityIface::SecurityTechnologyType::SPDM;
    }
    /** Set value of Type with option to skip sending signal */
    ComponentIntegrityIface::SecurityTechnologyType type(
        ComponentIntegrityIface::SecurityTechnologyType value, bool skipSignal)
    {
        return value;
    }

    /** Get value of TypeVersion */
    std::string typeVersion() const
    {
        return "1.1";
    }
    /** Set value of TypeVersion with option to skip sending signal */
    std::string typeVersion(std::string value, bool skipSignal)
    {
        return value;
    }
    /** Get value of LastUpdated */
    uint64_t lastUpdated() const
    {
        return 0;
    }
    /** Set value of LastUpdated with option to skip sending signal */
    uint64_t lastUpdated(uint64_t value, bool skipSignal)
    {
        return value;
    }
    /**
     * @brief Virtual destructor
     */
    virtual ~ComponentIntegrity() = default;

    void updateLastUpdateTime()
    {
        // last_updated(std::chrono::duration_cast<std::chrono::milliseconds>(
        //                  std::chrono::system_clock::now().time_since_epoch())
        //                  .count());
    }
    boost::asio::awaitable<MeasurementResult> method_call(
        MeasurementSetIface::spdm_get_signed_measurements_t,
        std::vector<size_t> measurementIndices, std::string nonce,
        size_t slotId)
    {
        auto spmdmtask = [this, measurementIndices, nonce,
                          slotId]() -> std::optional<MeasurementResult> {
            auto ret = requester->getSignedMeasurements(measurementIndices,
                                                        nonce, slotId);
            if (ret && ret.has_value())
            {
                auto& tupleValue = ret.value();
                return std::make_tuple(
                    sdbusplus::message::object_path("/some/path"),
                    std::get<0>(tupleValue), std::get<1>(tupleValue),
                    std::get<2>(tupleValue), std::get<3>(tupleValue),
                    std::get<4>(tupleValue));
            }

            return std::nullopt;
        };

        auto [ec, measurements] =
            co_await asyncCall<std::optional<MeasurementResult>>(
                ioContext, std::move(spmdmtask));
        if (ec || !measurements)
        {
            LOG_ERROR("Error getting measurements from SPDM: {}",
                      ec ? ec.message() : "operation failed");
            throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
        }
        co_return *measurements;
    }
    boost::asio::awaitable<std::tuple<bool, std::string>> method_call(
        MeasurementSetIface::exchange_certificate_t)
    {
        auto spmdmtask = [this]() -> std::optional<std::string> {
            auto result = exchangeCertificatesHelper();
            if (result)
            {
                return std::get<1>(*result);
            }
            return std::nullopt;
        };

        auto [ec, path] = co_await asyncCall<std::optional<std::string>>(
            ioContext, std::move(spmdmtask));
        if (ec || !path)
        {
            LOG_ERROR("Error in exchanging certificates: {}",
                      ec ? ec.message() : "operation failed");
            throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
        }
        LOG_INFO("Certificate exchanged successfully, path: {}", *path);
        co_return std::make_tuple(true, *path);
    }

    /**
     * @brief Helper function to exchange certificates
     * @param certPath Path to the certificate file
     * @return Optional tuple of (success, cert_path) if successful, nullopt
     * otherwise
     */
    std::optional<std::tuple<bool, std::string>> exchangeCertificatesHelper(
        const std::string& certPath = "/etc/ssl/certs/self_ca.pem")
    {
        auto [val, path] = requester->exchangeCertificates(certPath);
        if (!val)
        {
            LOG_ERROR("Failed to exchange certificates");
            return std::nullopt;
        }

        LOG_INFO("Certificate exchanged successfully, path: {}", path);
        return std::make_tuple(true, path);
    }

    /**
     * @brief Helper function to set provisioned state
     * @param provisioned The provisioned state to set
     * @return true if successful, false otherwise
     */
    bool setProvisionedState(bool provisioned)
    {
        bool result = requester->setProvisioned(provisioned);
        if (!result)
        {
            LOG_ERROR("Failed to set provisioned state to {}", provisioned);
        }
        return result;
    }

    virtual boost::asio::awaitable<bool> method_call(
        MeasurementSetIface::set_provisioned_t, bool provisioned)
    {
        auto spdmTask = [this, provisioned]() -> std::optional<bool> {
            // Send SET_PROVISIONED message to the SPDM responder
            if (setProvisionedState(provisioned))
            {
                return std::optional(true);
            }
            return std::nullopt;
        };

        auto [ec, success] = co_await asyncCall<std::optional<bool>>(
            ioContext, std::move(spdmTask));
        if (ec || !success)
        {
            LOG_ERROR("Error setting provisioned state: {}",
                      ec ? ec.message() : "operation failed");
            throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
        }
        co_return *success;
    }

    virtual boost::asio::awaitable<void> method_call(
        SecureExchangeIface::exchange_app_data_t)
    {
        auto spdmTask = [this]() -> std::optional<bool> {
            // Perform application data exchange over the active SPDM secure
            // session This uses the certificate exchange mechanism as the
            // underlying secure channel
            auto result = exchangeCertificatesHelper();
            if (!result)
            {
                LOG_ERROR(
                    "Failed to exchange application data over secure session");
                return std::nullopt;
            }

            // Set provisioned state to true after successful certificate
            // exchange
            LOG_INFO(
                "Setting provisioned state to true after successful exchange");
            if (!setProvisionedState(true))
            {
                return std::nullopt;
            }

            return std::optional(true);
        };

        auto [ec, success] = co_await asyncCall<std::optional<bool>>(
            ioContext, std::move(spdmTask));
        if (ec || !success)
        {
            LOG_ERROR("Error exchanging application data: {}",
                      ec ? ec.message() : "operation failed");
            throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
        }

        co_return;
    }

    static bool connectToResponder(std::shared_ptr<SpdmRequester>& requester,
                                   const DeviceInfo& deviceInfo)
    {
        std::string host;
        int port = 0;
        if (std::holds_alternative<TcpDeviceInfo>(deviceInfo.info))
        {
            const auto& tcpInfo = std::get<TcpDeviceInfo>(deviceInfo.info);
            host = tcpInfo.host;
            port = tcpInfo.port;
        }
        LOG_INFO("Entering spdm initiation for {}:{}", host, port);
        if (!requester->connect(host, port))
        {
            LOG_ERROR("Failed to connect to responder");
            return false;
        }
        LOG_INFO("Connected to responder");
        return true;
    }

    static bool initializeSpdmConnection(
        std::shared_ptr<SpdmRequester>& requester)
    {
        if (!requester->sayhello())
        {
            LOG_ERROR("Hello failed");
            return false;
        }
        LOG_INFO("Hello successful");
        if (!requester->init_connection())
        {
            LOG_ERROR("Error in spdm init connection");
            return false;
        }
        return true;
    }

    static void removeComponentIntegrity(const std::string& deviceId)
    {
        std::string path = std::format(CompIntegrityPath, deviceId);
        auto it = spdmDevices.find(path);
        if (it != spdmDevices.end())
        {
            LOG_INFO("Removing Component Integrity object at {}", path);
            spdmDevices.erase(it);
        }
    }
    static void handleConnectionError(
        boost::asio::io_context& ioContext,
        std::shared_ptr<sdbusplus::asio::connection> conn,
        DeviceInfo deviceInfo, SpdmTcpClient::CloseReason reason)
    {
        std::string reasonStr;
        switch (reason)
        {
            case SpdmTcpClient::CloseReason::SendFailed:
                reasonStr = "Send failed";
                break;
            case SpdmTcpClient::CloseReason::ReceiveFailed:
                reasonStr = "Receive failed";
                break;
            case SpdmTcpClient::CloseReason::ConnectionLost:
                reasonStr = "Connection lost";
                break;
        }
        LOG_ERROR("SPDM connection error for {}: {}", deviceInfo.id(),
                  reasonStr);

        // Just remove the component, don't recreate
        removeComponentIntegrity(deviceInfo.id());
        addComponentIntegrity(ioContext, conn, deviceInfo);
    }

    static void addComponentIntegrity(
        boost::asio::io_context& ioContext,
        std::shared_ptr<sdbusplus::asio::connection> conn,
        DeviceInfo deviceInfo, int retry = 0)
    {
        static constexpr int MAXTRY = 3;
        net::co_spawn(
            ioContext,
            [&ioContext, conn, deviceInfo = std::move(deviceInfo),
             retry]() -> net::awaitable<void> {
                auto spmdmtask = [&ioContext, conn, &deviceInfo]() {
                    auto requester = std::make_shared<SpdmRequester>(
                        ioContext, "/etc/ssl/certs/authority");

                    // Set up error callback for connection failures
                    requester->onClose([&ioContext, conn, deviceInfo](
                                           SpdmTcpClient::CloseReason reason) {
                        handleConnectionError(ioContext, conn, deviceInfo,
                                              reason);
                    });

                    if (!connectToResponder(requester, deviceInfo))
                    {
                        return std::shared_ptr<ComponentIntegrity>();
                    }

                    if (!initializeSpdmConnection(requester))
                    {
                        return std::shared_ptr<ComponentIntegrity>();
                    }

                    LOG_INFO("Create Component Integrity object");
                    auto compIntegrity = std::make_shared<ComponentIntegrity>(
                        ioContext, conn, std::move(requester), deviceInfo);
                    return compIntegrity;
                };
                auto [ec, integrity] =
                    co_await asyncCall<std::shared_ptr<ComponentIntegrity>>(
                        ioContext, std::move(spmdmtask));
                if (ec || !integrity)
                {
                    LOG_ERROR("Error executing SPDM task{}",
                              ec ? ec.message() : "Connection Error");
                    if (!integrity && retry < MAXTRY)
                    {
                        // try again.
                        co_await reactor::waitFor(ioContext.get_executor(), 5s);
                        addComponentIntegrity(ioContext, conn, deviceInfo,
                                              retry + 1);
                    }
                    co_return;
                }
                LOG_INFO(
                    "Component Integrity object created successfully at {}",
                    deviceInfo.id());
                spdmDevices[std::format(CompIntegrityPath, deviceInfo.id())] =
                    std::move(integrity);
                co_return;
            },
            net::detached);
    }
    static std::map<std::string, std::shared_ptr<ComponentIntegrity>>
        spdmDevices;
};

} // namespace spdm
