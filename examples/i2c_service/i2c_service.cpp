/**
 * @file i2c_service.cpp
 * @brief D-Bus service for I2C device access using coroutines
 *
 * This service exposes D-Bus methods to read and write I2C devices
 * using the coroutine-based I2CClient class.
 *
 * D-Bus Interface: com.ibm.I2C
 * D-Bus Object Path: /com/ibm/i2c
 * D-Bus Service Name: com.ibm.I2CService
 *
 * Methods:
 * - Write(s device_path, q slave_addr, ay data) -> (i bytes_written)
 * - Read(s device_path, q slave_addr, u num_bytes) -> (ay data)
 * - WriteRead(s device_path, q slave_addr, ay write_data, u read_bytes) -> (ay
 * read_data)
 */

#include "command_line_parser.hpp"
#include "i2c_client.hpp"
#include "logger.hpp"
#include "sdbus_calls.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace NSNAME;

/**
 * @brief PIC Pairing State values
 *
 * Represents the pairing state values returned by the PIC device over I2C.
 * This enum is trivially copyable and can be used with readTyped/writeTyped.
 */
enum class PairingState : uint8_t
{
    UNPAIRED = 0x00, ///< Device is not paired
    PAIRED = 0x55    ///< Device is paired
};

/**
 * @brief PIC Credentials Flag values
 *
 * Represents the credentials flag values returned by the PIC device over I2C.
 * This enum is trivially copyable and can be used with readTyped/writeTyped.
 */
enum class CredentialsFlag : uint8_t
{
    CLEAR_REQUIRED = 0x00,    ///< Credentials need to be cleared
    CLEAR_NOT_REQUIRED = 0x01 ///< Credentials do not need to be cleared
};

/**
 * @brief Convert PairingState to boolean
 */
constexpr bool toBool(PairingState state)
{
    return state == PairingState::PAIRED;
}

/**
 * @brief Convert PairingState to string
 */
constexpr const char* toString(PairingState state)
{
    return state == PairingState::PAIRED ? "PAIRED" : "UNPAIRED";
}

/**
 * @brief Convert CredentialsFlag to string
 */
constexpr const char* toString(CredentialsFlag flag)
{
    return flag == CredentialsFlag::CLEAR_REQUIRED
               ? "CLEAR_REQUIRED"
               : "CLEAR_NOT_REQUIRED";
}

/**
 * @brief PIC Version Information structure
 *
 * Represents the 6-byte version information returned by the PIC device.
 * This struct is trivially copyable and can be used with readTyped.
 */
struct PicVersion
{
    uint8_t bytes[6]; ///< 6 bytes of version information
};

/**
 * @brief PIC I2C Command structure
 *
 * Represents a 3-byte I2C command with header, command byte, and checksum.
 * Trivially copyable for use with writeTyped.
 */
struct PicCommand
{
    uint8_t header;   ///< Command header (always 0xFF)
    uint8_t command;  ///< Command byte
    uint8_t checksum; ///< Checksum byte

    /**
     * @brief Create a PIC command with automatic checksum calculation
     */
    static constexpr PicCommand create(uint8_t cmd)
    {
        return PicCommand{0xFF, cmd, static_cast<uint8_t>(~(0xFF ^ cmd))};
    }

    /**
     * @brief Command codes
     */
    static constexpr uint8_t CMD_GET_VERSION = 0x80;
    static constexpr uint8_t CMD_READ_PAIRING_STATE = 0x81;
    static constexpr uint8_t CMD_READ_CREDENTIALS_FLAG = 0x82;
    static constexpr uint8_t CMD_BMC_PAIRED = 0x50;
};

/**
 * @brief I2C Service Manager
 *
 * Manages I2C client instances and provides D-Bus method handlers
 */
class I2CServiceManager
{
  public:
    I2CServiceManager(net::any_io_executor executor) : executor_(executor) {}

    net::any_io_executor get_executor() const
    {
        return executor_;
    }

    /**
     * @brief Write data to an I2C device
     *
     * @param devicePath I2C device path (e.g., "/dev/i2c-1")
     * @param slaveAddr I2C slave address
     * @param data Data to write
     * @return Number of bytes written, or -1 on error
     */
    net::awaitable<int32_t> writeI2C(const std::string& devicePath,
                                     uint16_t slaveAddr,
                                     const std::vector<uint8_t>& data)
    {
        try
        {
            auto client = getOrCreateClient(devicePath, slaveAddr);

            // Ensure client is open
            if (!client->isOpen())
            {
                auto openResult = co_await client->open();
                if (openResult != I2CError::Success)
                {
                    LOG_ERROR("Failed to open I2C device {}: {}", devicePath,
                              to_string(openResult));
                    co_return -1;
                }
            }

            // Write data
            auto result = co_await client->write(data);
            if (!result)
            {
                LOG_ERROR("I2C write failed: {}", to_string(result.error()));
                co_return -1;
            }

            LOG_INFO("I2C write successful: {} bytes to {} at 0x{:02X}",
                     *result, devicePath, slaveAddr);
            co_return static_cast<int32_t>(*result);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception in writeI2C: {}", e.what());
            co_return -1;
        }
    }

    /**
     * @brief Read data from an I2C device
     *
     * @param devicePath I2C device path
     * @param slaveAddr I2C slave address
     * @param numBytes Number of bytes to read
     * @return Vector of bytes read, empty on error
     */
    net::awaitable<std::vector<uint8_t>> readI2C(
        const std::string& devicePath, uint16_t slaveAddr, uint32_t numBytes)
    {
        try
        {
            auto client = getOrCreateClient(devicePath, slaveAddr);

            // Ensure client is open
            if (!client->isOpen())
            {
                auto openResult = co_await client->open();
                if (openResult != I2CError::Success)
                {
                    LOG_ERROR("Failed to open I2C device {}: {}", devicePath,
                              to_string(openResult));
                    co_return std::vector<uint8_t>{};
                }
            }

            // Read data
            auto result = co_await client->read(numBytes);
            if (!result)
            {
                LOG_ERROR("I2C read failed: {}", to_string(result.error()));
                co_return std::vector<uint8_t>{};
            }

            LOG_INFO("I2C read successful: {} bytes from {} at 0x{:02X}",
                     result->size(), devicePath, slaveAddr);
            co_return *result;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception in readI2C: {}", e.what());
            co_return std::vector<uint8_t>{};
        }
    }

    /**
     * @brief Write then read from an I2C device (combined operation)
     *
     * @param devicePath I2C device path
     * @param slaveAddr I2C slave address
     * @param writeData Data to write first
     * @param readBytes Number of bytes to read after write
     * @return Vector of bytes read, empty on error
     */
    net::awaitable<std::vector<uint8_t>> writeReadI2C(
        const std::string& devicePath, uint16_t slaveAddr,
        const std::vector<uint8_t>& writeData, uint32_t readBytes)
    {
        try
        {
            auto client = getOrCreateClient(devicePath, slaveAddr);

            // Ensure client is open
            if (!client->isOpen())
            {
                auto openResult = co_await client->open();
                if (openResult != I2CError::Success)
                {
                    LOG_ERROR("Failed to open I2C device {}: {}", devicePath,
                              to_string(openResult));
                    co_return std::vector<uint8_t>{};
                }
            }

            // Write data
            auto writeResult = co_await client->write(writeData);
            if (!writeResult)
            {
                LOG_ERROR("I2C write failed in writeRead: {}",
                          to_string(writeResult.error()));
                co_return std::vector<uint8_t>{};
            }

            // Read data
            auto readResult = co_await client->read(readBytes);
            if (!readResult)
            {
                LOG_ERROR("I2C read failed in writeRead: {}",
                          to_string(readResult.error()));
                co_return std::vector<uint8_t>{};
            }

            LOG_INFO("I2C write-read successful: wrote {} bytes, read {} bytes "
                     "from {} at 0x{:02X}",
                     *writeResult, readResult->size(), devicePath, slaveAddr);
            co_return *readResult;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception in writeReadI2C: {}", e.what());
            co_return std::vector<uint8_t>{};
        }
    }

    /**
     * @brief Read PIC pairing state using typed read
     *
     * @param devicePath I2C device path (e.g., "/dev/i2c-3")
     * @param slaveAddr I2C slave address (e.g., 0x5a for PIC)
     * @return Pairing state as uint8_t (0x00=UNPAIRED, 0x55=PAIRED), or 0xFF on
     * error
     */
    net::awaitable<uint8_t> readPairingState(const std::string& devicePath,
                                             uint16_t slaveAddr)
    {
        try
        {
            auto client = getOrCreateClient(devicePath, slaveAddr);

            // Ensure client is open
            if (!client->isOpen())
            {
                auto openResult = co_await client->open();
                if (openResult != I2CError::Success)
                {
                    LOG_ERROR("Failed to open I2C device {}: {}", devicePath,
                              to_string(openResult));
                    co_return 0xFF;
                }
            }

            // Write READ_PAIRING_STATE command using typed write
            PicCommand cmd =
                PicCommand::create(PicCommand::CMD_READ_PAIRING_STATE);
            auto writeResult = co_await client->writeTyped(cmd);
            if (!writeResult)
            {
                LOG_ERROR("Failed to write READ_PAIRING_STATE command: {}",
                          to_string(writeResult.error()));
                co_return 0xFF;
            }

            // Read pairing state using typed read
            auto readResult = co_await client->readTyped<PairingState>();
            if (!readResult)
            {
                LOG_ERROR("Failed to read pairing state: {}",
                          to_string(readResult.error()));
                co_return 0xFF;
            }

            PairingState state = *readResult;
            LOG_INFO("Read pairing state from {} at 0x{:02X}: {} (0x{:02X})",
                     devicePath, slaveAddr, toString(state),
                     static_cast<uint8_t>(state));
            co_return static_cast<uint8_t>(state);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception in readPairingState: {}", e.what());
            co_return 0xFF;
        }
    }

    /**
     * @brief Write PIC pairing state (set to PAIRED) using typed write
     *
     * @param devicePath I2C device path (e.g., "/dev/i2c-3")
     * @param slaveAddr I2C slave address (e.g., 0x5a for PIC)
     * @return true if successful, false otherwise
     */
    net::awaitable<bool> writePairingState(const std::string& devicePath,
                                           uint16_t slaveAddr)
    {
        try
        {
            auto client = getOrCreateClient(devicePath, slaveAddr);

            // Ensure client is open
            if (!client->isOpen())
            {
                auto openResult = co_await client->open();
                if (openResult != I2CError::Success)
                {
                    LOG_ERROR("Failed to open I2C device {}: {}", devicePath,
                              to_string(openResult));
                    co_return false;
                }
            }

            // Write BMC_PAIRED command using typed write
            PicCommand cmd = PicCommand::create(PicCommand::CMD_BMC_PAIRED);
            auto writeResult = co_await client->writeTyped(cmd);
            if (!writeResult)
            {
                LOG_ERROR("Failed to set PAIRED state: {}",
                          to_string(writeResult.error()));
                co_return false;
            }

            LOG_INFO(
                "Successfully set pairing state to PAIRED on {} at 0x{:02X}",
                devicePath, slaveAddr);
            co_return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception in writePairingState: {}", e.what());
            co_return false;
        }
    }

    /**
     * @brief Read PIC credentials flag using typed read
     *
     * @param devicePath I2C device path (e.g., "/dev/i2c-3")
     * @param slaveAddr I2C slave address (e.g., 0x5a for PIC)
     * @return Credentials flag as uint8_t (0x00=CLEAR_REQUIRED,
     * 0x01=CLEAR_NOT_REQUIRED), or 0xFF on error
     */
    net::awaitable<uint8_t> readCredentialsFlag(const std::string& devicePath,
                                                uint16_t slaveAddr)
    {
        try
        {
            auto client = getOrCreateClient(devicePath, slaveAddr);

            // Ensure client is open
            if (!client->isOpen())
            {
                auto openResult = co_await client->open();
                if (openResult != I2CError::Success)
                {
                    LOG_ERROR("Failed to open I2C device {}: {}", devicePath,
                              to_string(openResult));
                    co_return 0xFF;
                }
            }

            // Write READ_CREDENTIALS_FLAG command using typed write
            PicCommand cmd =
                PicCommand::create(PicCommand::CMD_READ_CREDENTIALS_FLAG);
            auto writeResult = co_await client->writeTyped(cmd);
            if (!writeResult)
            {
                LOG_ERROR("Failed to write READ_CREDENTIALS_FLAG command: {}",
                          to_string(writeResult.error()));
                co_return 0xFF;
            }

            // Read credentials flag using typed read
            auto readResult = co_await client->readTyped<CredentialsFlag>();
            if (!readResult)
            {
                LOG_ERROR("Failed to read credentials flag: {}",
                          to_string(readResult.error()));
                co_return 0xFF;
            }

            CredentialsFlag flag = *readResult;
            LOG_INFO("Read credentials flag from {} at 0x{:02X}: {} (0x{:02X})",
                     devicePath, slaveAddr, toString(flag),
                     static_cast<uint8_t>(flag));
            co_return static_cast<uint8_t>(flag);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception in readCredentialsFlag: {}", e.what());
            co_return 0xFF;
        }
    }

    /**
     * @brief Read PIC version information using typed read
     *
     * @param devicePath I2C device path (e.g., "/dev/i2c-3")
     * @param slaveAddr I2C slave address (e.g., 0x5a for PIC)
     * @return Vector of 6 bytes containing version info, empty on error
     */
    net::awaitable<std::vector<uint8_t>> readVersion(
        const std::string& devicePath, uint16_t slaveAddr)
    {
        try
        {
            auto client = getOrCreateClient(devicePath, slaveAddr);

            // Ensure client is open
            if (!client->isOpen())
            {
                auto openResult = co_await client->open();
                if (openResult != I2CError::Success)
                {
                    LOG_ERROR("Failed to open I2C device {}: {}", devicePath,
                              to_string(openResult));
                    co_return std::vector<uint8_t>{};
                }
            }

            // Write GET_VERSION command using typed write
            PicCommand cmd = PicCommand::create(PicCommand::CMD_GET_VERSION);
            auto writeResult = co_await client->writeTyped(cmd);
            if (!writeResult)
            {
                LOG_ERROR("Failed to write GET_VERSION command: {}",
                          to_string(writeResult.error()));
                co_return std::vector<uint8_t>{};
            }

            // Read 6 bytes of version information using typed read
            auto readResult = co_await client->readTyped<PicVersion>();
            if (!readResult)
            {
                LOG_ERROR("Failed to read version info: {}",
                          to_string(readResult.error()));
                co_return std::vector<uint8_t>{};
            }

            PicVersion version = *readResult;
            std::vector<uint8_t> versionBytes(version.bytes, version.bytes + 6);

            LOG_INFO(
                "Read version from {} at 0x{:02X}: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
                devicePath, slaveAddr, version.bytes[0], version.bytes[1],
                version.bytes[2], version.bytes[3], version.bytes[4],
                version.bytes[5]);

            co_return versionBytes;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception in readVersion: {}", e.what());
            co_return std::vector<uint8_t>{};
        }
    }

  private:
    /**
     * @brief Get or create an I2C client for the given device and address
     */
    std::shared_ptr<I2CClient> getOrCreateClient(const std::string& devicePath,
                                                 uint16_t slaveAddr)
    {
        std::string key = devicePath + ":" + std::to_string(slaveAddr);

        auto it = clients_.find(key);
        if (it != clients_.end())
        {
            return it->second;
        }

        // Create new client
        auto client =
            std::make_shared<I2CClient>(executor_, devicePath, slaveAddr);
        clients_[key] = client;
        LOG_INFO("Created new I2C client for {} at 0x{:02X}", devicePath,
                 slaveAddr);
        return client;
    }

    net::any_io_executor executor_;
    std::map<std::string, std::shared_ptr<I2CClient>> clients_;
};

int main(int argc, const char* argv[])
{
    try
    {
        // Parse command line arguments
        auto args = parseCommandline(argc, argv);
        auto [help] = getArgs(args, "--help,-h");

        if (help)
        {
            std::cout
                << "I2C D-Bus Service\n"
                << "Usage: " << argv[0] << " [options]\n"
                << "Options:\n"
                << "  --help, -h    Show this help message\n"
                << "\n"
                << "D-Bus Interface:\n"
                << "  Service: com.ibm.I2CService\n"
                << "  Object:  /com/ibm/i2c\n"
                << "  Interface: com.ibm.I2C\n"
                << "\n"
                << "Methods:\n"
                << "  Write(s device_path, q slave_addr, ay data) -> i\n"
                << "  Read(s device_path, q slave_addr, u num_bytes) -> ay\n"
                << "  WriteRead(s device_path, q slave_addr, ay write_data, "
                   "u read_bytes) -> ay\n"
                << "  ReadPairingState(s device_path, q slave_addr) -> y\n"
                << "  WritePairingState(s device_path, q slave_addr) -> b\n"
                << "  ReadCredentialsFlag(s device_path, q slave_addr) -> y\n"
                << "  ReadVersion(s device_path, q slave_addr) -> ay\n"
                << "\n"
                << "PIC Typed Methods:\n"
                << "  ReadPairingState: Read PIC pairing state using typed I2C read\n"
                << "    Returns: 0x00=UNPAIRED, 0x55=PAIRED, 0xFF=ERROR\n"
                << "  WritePairingState: Set PIC to PAIRED state using typed I2C write\n"
                << "    Returns: true on success, false on failure\n"
                << "  ReadCredentialsFlag: Read PIC credentials flag using typed I2C read\n"
                << "    Returns: 0x00=CLEAR_REQUIRED, 0x01=CLEAR_NOT_REQUIRED, 0xFF=ERROR\n"
                << "  ReadVersion: Read PIC version information using typed I2C read\n"
                << "    Returns: 6 bytes of version info, empty array on error\n";
            return 0;
        }

        // Initialize logging
        LOG_INFO("Starting I2C D-Bus Service");

        // Create io_context
        boost::asio::io_context io_context;

        // Create D-Bus connection
        auto conn = std::make_shared<sdbusplus::asio::connection>(io_context);

        // Request D-Bus service name
        constexpr std::string_view busName = "com.ibm.I2CService";
        constexpr std::string_view objPath = "/com/ibm/i2c";
        constexpr std::string_view interface = "com.ibm.I2C";

        conn->request_name(busName.data());
        LOG_INFO("Requested D-Bus name: {}", busName);

        // Create object server
        auto dbusServer = sdbusplus::asio::object_server(conn);
        std::shared_ptr<sdbusplus::asio::dbus_interface> iface =
            dbusServer.add_interface(objPath.data(), interface.data());

        // Create I2C service manager
        auto manager =
            std::make_shared<I2CServiceManager>(io_context.get_executor());

        // Register Write method
        iface->register_method(
            "Write",
            [manager,
             &io_context](const std::string& devicePath, uint16_t slaveAddr,
                          const std::vector<uint8_t>& data) -> int32_t {
                LOG_INFO(
                    "D-Bus Write request: device={}, addr=0x{:02X}, bytes={}",
                    devicePath, slaveAddr, data.size());
                auto future = net::co_spawn(
                    manager->get_executor(),
                    manager->writeI2C(devicePath, slaveAddr, data),
                    net::use_future);
                // Poll io_context while waiting for the future
                while (future.wait_for(std::chrono::milliseconds(0)) !=
                       std::future_status::ready)
                {
                    io_context.poll_one();
                }
                auto result = future.get();
                LOG_INFO("D-Bus Write completed: result={}", result);
                return result;
            });

        // Register Read method
        iface->register_method(
            "Read",
            [manager,
             &io_context](const std::string& devicePath, uint16_t slaveAddr,
                          uint32_t numBytes) -> std::vector<uint8_t> {
                LOG_INFO(
                    "D-Bus Read request: device={}, addr=0x{:02X}, bytes={}",
                    devicePath, slaveAddr, numBytes);
                auto future = net::co_spawn(
                    manager->get_executor(),
                    manager->readI2C(devicePath, slaveAddr, numBytes),
                    net::use_future);
                // Poll io_context while waiting for the future
                while (future.wait_for(std::chrono::milliseconds(0)) !=
                       std::future_status::ready)
                {
                    io_context.poll_one();
                }
                auto result = future.get();
                LOG_INFO("D-Bus Read completed: received {} bytes",
                         result.size());
                return result;
            });

        // Register WriteRead method
        iface->register_method(
            "WriteRead",
            [manager,
             &io_context](const std::string& devicePath, uint16_t slaveAddr,
                          const std::vector<uint8_t>& writeData,
                          uint32_t readBytes) -> std::vector<uint8_t> {
                LOG_INFO("D-Bus WriteRead request: device={}, addr=0x{:02X}, "
                         "write={} bytes, read={} bytes",
                         devicePath, slaveAddr, writeData.size(), readBytes);
                auto future =
                    net::co_spawn(manager->get_executor(),
                                  manager->writeReadI2C(devicePath, slaveAddr,
                                                        writeData, readBytes),
                                  net::use_future);
                // Poll io_context while waiting for the future
                while (future.wait_for(std::chrono::milliseconds(0)) !=
                       std::future_status::ready)
                {
                    io_context.poll_one();
                }
                auto result = future.get();
                LOG_INFO("D-Bus WriteRead completed: received {} bytes",
                         result.size());
                return result;
            });

        // Register ReadPairingState method
        iface->register_method(
            "ReadPairingState",
            [manager, &io_context](const std::string& devicePath,
                                   uint16_t slaveAddr) -> uint8_t {
                LOG_INFO(
                    "D-Bus ReadPairingState request: device={}, addr=0x{:02X}",
                    devicePath, slaveAddr);
                auto future = net::co_spawn(
                    manager->get_executor(),
                    manager->readPairingState(devicePath, slaveAddr),
                    net::use_future);
                // Poll io_context while waiting for the future
                while (future.wait_for(std::chrono::milliseconds(0)) !=
                       std::future_status::ready)
                {
                    io_context.poll_one();
                }
                auto result = future.get();
                LOG_INFO("D-Bus ReadPairingState completed: state=0x{:02X}",
                         result);
                return result;
            });

        // Register WritePairingState method
        iface->register_method(
            "WritePairingState",
            [manager, &io_context](const std::string& devicePath,
                                   uint16_t slaveAddr) -> bool {
                LOG_INFO(
                    "D-Bus WritePairingState request: device={}, addr=0x{:02X}",
                    devicePath, slaveAddr);
                auto future = net::co_spawn(
                    manager->get_executor(),
                    manager->writePairingState(devicePath, slaveAddr),
                    net::use_future);
                // Poll io_context while waiting for the future
                while (future.wait_for(std::chrono::milliseconds(0)) !=
                       std::future_status::ready)
                {
                    io_context.poll_one();
                }
                auto result = future.get();
                LOG_INFO("D-Bus WritePairingState completed: success={}",
                         result);
                return result;
            });

        // Register ReadCredentialsFlag method
        iface->register_method(
            "ReadCredentialsFlag",
            [manager, &io_context](const std::string& devicePath,
                                   uint16_t slaveAddr) -> uint8_t {
                LOG_INFO(
                    "D-Bus ReadCredentialsFlag request: device={}, addr=0x{:02X}",
                    devicePath, slaveAddr);
                auto future = net::co_spawn(
                    manager->get_executor(),
                    manager->readCredentialsFlag(devicePath, slaveAddr),
                    net::use_future);
                // Poll io_context while waiting for the future
                while (future.wait_for(std::chrono::milliseconds(0)) !=
                       std::future_status::ready)
                {
                    io_context.poll_one();
                }
                auto result = future.get();
                LOG_INFO("D-Bus ReadCredentialsFlag completed: flag=0x{:02X}",
                         result);
                return result;
            });

        // Register ReadVersion method
        iface->register_method(
            "ReadVersion",
            [manager, &io_context](const std::string& devicePath,
                                   uint16_t slaveAddr) -> std::vector<uint8_t> {
                LOG_INFO("D-Bus ReadVersion request: device={}, addr=0x{:02X}",
                         devicePath, slaveAddr);
                auto future =
                    net::co_spawn(manager->get_executor(),
                                  manager->readVersion(devicePath, slaveAddr),
                                  net::use_future);
                // Poll io_context while waiting for the future
                while (future.wait_for(std::chrono::milliseconds(0)) !=
                       std::future_status::ready)
                {
                    io_context.poll_one();
                }
                auto result = future.get();
                LOG_INFO("D-Bus ReadVersion completed: received {} bytes",
                         result.size());
                return result;
            });

        // Initialize the interface
        iface->initialize();
        LOG_INFO("D-Bus interface initialized at {}", objPath);

        // Run the io_context
        LOG_INFO("I2C Service running. Press Ctrl+C to exit.");
        io_context.run();

        LOG_INFO("I2C Service stopped");
        return 0;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Fatal error: {}", e.what());
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
