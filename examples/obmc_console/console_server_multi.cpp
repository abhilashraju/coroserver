/**
 * @file console_server_multi.cpp
 * @brief OBMC Console Server - Multi-device support
 *
 * This server extends the basic console server to support multiple devices:
 * - Multiple UART/VUART/PTY devices simultaneously
 * - Each device has its own Unix socket
 * - Independent ringbuffers for each console
 * - D-Bus interface for each console instance
 * - GPIO-based multiplexing support
 *
 * Usage:
 *   console_server_multi <config-file>
 *   console_server_multi /etc/obmc-console-multi.conf
 */

#include "command_line_parser.hpp"
#include "console_config.hpp"
#include "console_dbus.hpp"
#include "logger.hpp"
#include "pty_device.hpp"
#include "tcp_server.hpp"
#include "uart_server.hpp"

#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/circular_buffer.hpp>
#include <sdbusplus/asio/connection.hpp>

#include <csignal>
#include <memory>
#include <set>
#include <stop_token>
#include <vector>

using namespace NSNAME;
using unix_socket = boost::asio::local::stream_protocol;
using plain_socket = unix_socket::socket;

// Global stop source for cooperative cancellation
std::stop_source globalStopSource;

void signalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        LOG_INFO("Shutdown signal received");
        globalStopSource.request_stop();
    }
}

/**
 * @brief Type alias for console history buffer
 */
using RingBuffer = boost::circular_buffer<char>;

/**
 * @brief Console router that handles client connections for a specific device
 */
/**
 * @brief Type-erased client wrapper for broadcasting
 */
class ClientWriter
{
  public:
    virtual ~ClientWriter() = default;
    virtual net::awaitable<void> write(const std::vector<char>& data) = 0;
    virtual bool isOpen() const = 0;
};

template <typename StreamType>
class ClientWriterImpl : public ClientWriter
{
  public:
    ClientWriterImpl(TimedStreamer<StreamType> streamer) :
        streamer_(std::move(streamer))
    {}

    net::awaitable<void> write(const std::vector<char>& data) override
    {
        auto [ec, bytes] =
            co_await streamer_.write(boost::asio::buffer(data), false);
        if (ec)
        {
            LOG_ERROR("Failed to write to client: {}", ec.message());
        }
    }

    bool isOpen() const override
    {
        return streamer_.isOpen();
    }

  private:
    TimedStreamer<StreamType> streamer_;
};

class ConsoleRouter
{
  public:
    ConsoleRouter(net::any_io_executor io_context, RingBuffer& ringBuffer,
                  std::unique_ptr<UartDevice>& uart,
                  std::unique_ptr<PtyDevice>& pty, const std::string& name,
                  std::stop_token stopToken) :
        io_context_(io_context), ringBuffer_(ringBuffer), uart_(uart),
        pty_(pty), consoleName_(name), stopToken_(stopToken)
    {}

    /**
     * @brief Handle read errors and determine if connection should terminate
     * @param ec The error code from the read operation
     * @param streamer The client streamer
     * @param myId Client ID for logging
     * @return true if connection should terminate, false for genuine timeouts
     */
    template <typename StreamType>
    bool shouldTerminateConnection(const boost::system::error_code& ec,
                                   const TimedStreamer<StreamType>& streamer,
                                   int myId)
    {
        // Handle timeout/operation_aborted specially
        if (ec == boost::asio::error::operation_aborted)
        {
            LOG_DEBUG("[{}] Client {} read timed out", consoleName_, myId);

            // Check if socket is actually closed after timeout
            if (!streamer.socket->is_open())
            {
                LOG_INFO(
                    "[{}] Client {} socket closed (detected after timeout)",
                    consoleName_, myId);
                return true; // Terminate connection
            }

            // IMPORTANT: When operation_aborted occurs, we cannot reliably
            // distinguish between a genuine timeout and a closed socket
            // because is_open() may not immediately reflect the closed state.
            //
            // The next read attempt will definitively detect if the socket
            // is closed and return an appropriate error (EOF, connection_reset,
            // etc.)
            //
            // Therefore, we return false here to allow one more read attempt,
            // which will properly detect and handle the closed connection.
            return false; // Continue to next read to detect actual state
        }

        // All other errors terminate the connection
        if (ec != boost::asio::error::eof)
        {
            LOG_ERROR("[{}] Client {} read error: {}", consoleName_, myId,
                      ec.message());
        }
        return true;
    }
    template <typename StreamType>
    net::awaitable<void> operator()(TimedStreamer<StreamType> streamer,
                                    int clientFd)
    {
        return operator()(streamer);
    }
    template <typename StreamType>
    net::awaitable<void> operator()(TimedStreamer<StreamType> streamer)
    {
        static std::atomic<int> clientId{1};
        int myId = clientId++;

        // Add client to list (supports all stream types: Unix socket and
        // D-Bus)
        auto clientWriter =
            std::make_shared<ClientWriterImpl<StreamType>>(streamer);
        clients_.push_back(clientWriter);

        LOG_INFO("[{}] Client {} connected (total clients: {})", consoleName_,
                 myId, clients_.size());

        // Send console history to new client
        if (!ringBuffer_.empty())
        {
            LOG_DEBUG("Sending History");
            std::vector<char> history(ringBuffer_.begin(), ringBuffer_.end());
            auto [ec, bytes] =
                co_await streamer.write(boost::asio::buffer(history));
            if (ec)
            {
                LOG_ERROR("[{}] Failed to send history to client {}: {}",
                          consoleName_, myId, ec.message());
            }
        }

        // Read from client and forward to UART
        std::array<char, 1024> buffer;
        while (!stopToken_.stop_requested())
        {
            auto [ec, bytesRead] =
                co_await streamer.read(boost::asio::buffer(buffer), false);
            if (ec)
            {
                if (shouldTerminateConnection(ec, streamer, myId))
                {
                    break;
                }
                continue; // Genuine timeout, keep reading
            }

            // Check for EOF: 0 bytes read with no error means peer closed
            if (bytesRead == 0)
            {
                LOG_INFO("[{}] Client {} closed connection (EOF)", consoleName_,
                         myId);
                break;
            }

            if (bytesRead > 0)
            {
                std::vector<char> data(buffer.begin(),
                                       buffer.begin() + bytesRead);

                // Write to UART or PTY device
                if (uart_ && uart_->isOpen())
                {
                    boost::asio::co_spawn(
                        io_context_,
                        [this,
                         data = std::move(data)]() -> net::awaitable<void> {
                            co_await uart_->write(boost::asio::buffer(data));
                        },
                        boost::asio::detached);
                }
                else if (pty_ && pty_->isOpen())
                {
                    boost::asio::co_spawn(
                        io_context_,
                        [this,
                         data = std::move(data)]() -> net::awaitable<void> {
                            co_await pty_->write(boost::asio::buffer(data));
                        },
                        boost::asio::detached);
                }
            }
        }

        // Remove this client from the list
        removeClient(clientWriter);
        LOG_INFO("[{}] Client {} disconnected (remaining clients: {})",
                 consoleName_, myId, clients_.size());
    }

    /**
     * @brief Broadcast data to all connected clients (Unix socket and
     * D-Bus)
     */
    void broadcastToAll(const std::vector<char>& data)
    {
        // Remove closed clients before broadcasting
        removeClosedClients();

        // Broadcast to all remaining clients
        for (auto& client : clients_)
        {
            boost::asio::co_spawn(io_context_, client->write(data),
                                  boost::asio::detached);
        }
    }

  private:
    /**
     * @brief Remove a specific client from the list
     */
    void removeClient(const std::shared_ptr<ClientWriter>& client)
    {
        clients_.erase(std::remove(clients_.begin(), clients_.end(), client),
                       clients_.end());
    }

    /**
     * @brief Remove all closed clients from the list
     */
    void removeClosedClients()
    {
        clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                      [](const auto& client) {
                                          return !client->isOpen();
                                      }),
                       clients_.end());
    }

    net::any_io_executor io_context_;
    RingBuffer& ringBuffer_;
    std::vector<std::shared_ptr<ClientWriter>> clients_;
    std::unique_ptr<UartDevice>& uart_;
    std::unique_ptr<PtyDevice>& pty_;
    std::string consoleName_;
    std::stop_token stopToken_;
};

/**
 * @brief Single console instance managing one device
 */
class ConsoleInstance
{
  public:
    ConsoleInstance(net::any_io_executor io_context,
                    const DeviceConfig& deviceConfig,
                    std::shared_ptr<sdbusplus::asio::connection> sharedBus,
                    std::stop_token stopToken) :
        io_context_(io_context), deviceConfig_(deviceConfig),
        acceptor_(io_context, deviceConfig.socketPath),
        ringBuffer_(128 * 1024), // 128KB buffer
        router_(io_context, ringBuffer_, uart_, pty_, deviceConfig.name,
                stopToken),
        server_(io_context, acceptor_, router_), bus_(sharedBus),
        stopToken_(stopToken)
    {}

    boost::system::error_code start()
    {
        LOG_INFO("[{}] Unix socket created: {}", deviceConfig_.name,
                 deviceConfig_.socketPath);
        LOG_INFO("[{}] Device: {} (type: {})", deviceConfig_.name,
                 deviceConfig_.device, getDeviceTypeString());
        LOG_INFO("[{}] Baud rate: {}", deviceConfig_.name,
                 deviceConfig_.baudRate);

        // Initialize D-Bus interface (creates its own connection)
        initializeDbusInterface();

        // Start device handler
        boost::asio::co_spawn(io_context_, handleDevice(),
                              boost::asio::detached);

        return boost::system::error_code{};
    }

    void stop()
    {
        if (uart_)
        {
            uart_->close();
        }
        if (pty_)
        {
            pty_->close();
        }
        LOG_INFO("[{}] Console stopped", deviceConfig_.name);
    }

    const std::string& getName() const
    {
        return deviceConfig_.name;
    }

  private:
    std::string getDeviceTypeString() const
    {
        switch (deviceConfig_.type)
        {
            case DeviceType::UART:
                return "UART";
            case DeviceType::VUART:
                return "VUART";
            case DeviceType::PTY:
                return "PTY";
            default:
                return "Unknown";
        }
    }

    /**
     * @brief Device handler function type
     */
    using DeviceHandler = std::function<net::awaitable<void>()>;

    /**
     * @brief Device handler table entry
     */
    struct DeviceHandlerEntry
    {
        DeviceType type;
        DeviceHandler handler;
    };

    /**
     * @brief Initialize UART/VUART device
     */
    net::awaitable<void> initUartDevice()
    {
        UartConfig uartConfig;
        uartConfig.device = deviceConfig_.device;
        uartConfig.baudRate = parseBaudRate(deviceConfig_.baudRate);
        uartConfig.rawMode = true;
        uartConfig.enableParity = false;
        uartConfig.stopBits = 1;
        uartConfig.dataBits = 8;
        uartConfig.hardwareFlowControl = false;

        uart_ = std::make_unique<UartDevice>(io_context_, uartConfig);

        auto ec = uart_->open();
        if (ec)
        {
            LOG_ERROR("[{}] Failed to open device: {}", deviceConfig_.name,
                      ec.message());
            co_return;
        }

        LOG_INFO("[{}] Device opened successfully", deviceConfig_.name);

        // Configure VUART-specific sysfs attributes if this is a VUART device
        if (deviceConfig_.type == DeviceType::VUART)
        {
            configureVuartSysfs();
        }

        co_await deviceReadLoop(
            [this](auto buffer)
                -> net::awaitable<
                    std::pair<boost::system::error_code, std::size_t>> {
                co_return co_await uart_->read(buffer);
            });

        LOG_INFO("[{}] Device handler stopped", deviceConfig_.name);
    }

    /**
     * @brief Initialize PTY device
     */
    net::awaitable<void> initPtyDevice()
    {
        PtyConfig ptyConfig;
        ptyConfig.shellPath = deviceConfig_.shellPath.empty()
                                  ? "/bin/sh"
                                  : deviceConfig_.shellPath;
        ptyConfig.shellArgs = deviceConfig_.shellArgs;
        ptyConfig.spawnShell = true;

        pty_ = std::make_unique<PtyDevice>(io_context_, ptyConfig);

        auto ec = pty_->open();
        if (ec)
        {
            LOG_ERROR("[{}] Failed to open PTY: {}", deviceConfig_.name,
                      ec.message());
            co_return;
        }

        LOG_INFO("[{}] PTY opened successfully: {}", deviceConfig_.name,
                 pty_->getSlaveName());
        if (pty_->getShellPid() > 0)
        {
            LOG_INFO("[{}] Shell process started (PID: {})", deviceConfig_.name,
                     pty_->getShellPid());
        }

        co_await deviceReadLoop(
            [this](auto buffer)
                -> net::awaitable<
                    std::pair<boost::system::error_code, std::size_t>> {
                co_return co_await pty_->read(buffer);
            });

        LOG_INFO("[{}] PTY handler stopped", deviceConfig_.name);
    }

    /**
     * @brief Get device handler table
     */
    std::vector<DeviceHandlerEntry> getDeviceHandlerTable()
    {
        return {{DeviceType::UART, [this]() { return initUartDevice(); }},
                {DeviceType::VUART, [this]() { return initUartDevice(); }},
                {DeviceType::PTY, [this]() { return initPtyDevice(); }}};
    }

    /**
     * @brief Main device handler using table-driven approach
     */
    net::awaitable<void> handleDevice()
    {
        auto handlerTable = getDeviceHandlerTable();

        // Find handler for current device type
        for (const auto& entry : handlerTable)
        {
            if (entry.type == deviceConfig_.type)
            {
                co_await entry.handler();
                co_return;
            }
        }

        // No handler found for device type
        LOG_ERROR("[{}] No handler found for device type: {}",
                  deviceConfig_.name, getDeviceTypeString());
    }

    template <typename ReadFunc>
    net::awaitable<void> deviceReadLoop(ReadFunc readFunc)
    {
        std::array<char, 1024> buffer;

        while (!stopToken_.stop_requested())
        {
            // Read from device (UART or PTY)
            auto [readEc,
                  bytesRead] = co_await readFunc(boost::asio::buffer(buffer));

            if (readEc)
            {
                if (readEc != boost::asio::error::operation_aborted)
                {
                    LOG_ERROR("[{}] Device read error: {}", deviceConfig_.name,
                              readEc.message());
                }
                break;
            }

            if (bytesRead > 0)
            {
                std::vector<char> data(buffer.begin(),
                                       buffer.begin() + bytesRead);

                // Add to ringbuffer
                ringBuffer_.insert(ringBuffer_.end(), data.begin(), data.end());

                // Broadcast to all clients (Unix socket and D-Bus)
                router_.broadcastToAll(data);
            }
        }
    }

    /**
     * @brief Configure VUART sysfs attributes (lpc_address and sirq)
     */
    void configureVuartSysfs()
    {
        LOG_INFO("[{}] Configuring VUART sysfs attributes", deviceConfig_.name);

        // Parse and configure LPC address
        if (!deviceConfig_.lpcAddress.empty())
        {
            try
            {
                // Parse hex string (e.g., "0x3f8")
                uint16_t lpcAddr = static_cast<uint16_t>(
                    std::stoul(deviceConfig_.lpcAddress, nullptr, 0));
                auto ec =
                    uart_->configureSysfsAttribute("lpc_address", lpcAddr);
                if (ec)
                {
                    LOG_WARNING("[{}] Failed to configure lpc_address: {}",
                                deviceConfig_.name, ec.message());
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("[{}] Invalid lpc_address value '{}': {}",
                          deviceConfig_.name, deviceConfig_.lpcAddress,
                          e.what());
            }
        }

        // Configure SIRQ
        if (deviceConfig_.sirq > 0)
        {
            auto ec = uart_->configureSysfsAttribute(
                "sirq", static_cast<uint16_t>(deviceConfig_.sirq));
            if (ec)
            {
                LOG_WARNING("[{}] Failed to configure sirq: {}",
                            deviceConfig_.name, ec.message());
            }
        }
    }

    void initializeDbusInterface()
    {
        // Check if shared D-Bus connection is available
        if (!bus_)
        {
            LOG_WARNING(
                "[{}] No D-Bus connection available - running without D-Bus support",
                deviceConfig_.name);
            return;
        }

        auto getBaud = [this]() -> speed_t {
            return parseBaudRate(deviceConfig_.baudRate);
        };

        auto setBaud = [this](speed_t baud) {
            deviceConfig_.baudRate = baudRateToInt(baud);
            if (uart_ && uart_->isOpen())
            {
                // Reconfigure UART with new baud rate
                uart_->close();
                auto ec = uart_->open();
                if (ec)
                {
                    LOG_ERROR("[{}] Failed to reopen device with new baud: {}",
                              deviceConfig_.name, ec.message());
                }
            }
        };

        dbusInterface_ = std::make_unique<ConsoleDbusInterface<ConsoleRouter>>(
            io_context_, bus_, deviceConfig_.name, getBaud, setBaud, router_);

        if (!dbusInterface_->initialize())
        {
            LOG_ERROR("[{}] Failed to initialize D-Bus interface",
                      deviceConfig_.name);
            dbusInterface_.reset();
        }
    }

    static speed_t parseBaudRate(int baud)
    {
        switch (baud)
        {
            case 9600:
                return B9600;
            case 19200:
                return B19200;
            case 38400:
                return B38400;
            case 57600:
                return B57600;
            case 115200:
                return B115200;
            case 230400:
                return B230400;
            default:
                LOG_WARNING("Unsupported baud rate {}, using 115200", baud);
                return B115200;
        }
    }

    static int baudRateToInt(speed_t baud)
    {
        switch (baud)
        {
            case B9600:
                return 9600;
            case B19200:
                return 19200;
            case B38400:
                return 38400;
            case B57600:
                return 57600;
            case B115200:
                return 115200;
            case B230400:
                return 230400;
            default:
                return 115200;
        }
    }

    net::any_io_executor io_context_;
    DeviceConfig deviceConfig_;
    UnixStreamTypePlain acceptor_;
    RingBuffer ringBuffer_;
    std::unique_ptr<UartDevice> uart_;
    std::unique_ptr<PtyDevice> pty_;
    ConsoleRouter router_;
    TcpServer<UnixStreamTypePlain, ConsoleRouter> server_;
    std::shared_ptr<sdbusplus::asio::connection> bus_;
    std::unique_ptr<ConsoleDbusInterface<ConsoleRouter>> dbusInterface_;
    std::stop_token stopToken_;
};

/**
 * @brief Multi-device console server manager
 */
class MultiConsoleServer
{
  public:
    MultiConsoleServer(net::any_io_executor io_context,
                       const ConsoleConfig& config) :
        io_context_(io_context), config_(config)
    {}

    boost::system::error_code start()
    {
        LOG_INFO("Starting multi-device console server");
        LOG_INFO("Number of devices: {}", config_.devices.size());

        // Create shared D-Bus connection for all console instances
        try
        {
            auto& io_ctx = static_cast<net::io_context&>(io_context_.context());
            sharedBus_ = std::make_shared<sdbusplus::asio::connection>(io_ctx);
            LOG_INFO("Shared D-Bus connection established");
        }
        catch (const sdbusplus::exception_t& e)
        {
            LOG_WARNING(
                "D-Bus not available: {} - consoles will run without D-Bus support",
                e.what());
        }
        catch (const std::exception& e)
        {
            LOG_WARNING(
                "D-Bus connection failed: {} - consoles will run without D-Bus support",
                e.what());
        }

        for (const auto& deviceConfig : config_.devices)
        {
            // Remove existing socket file
            ::unlink(deviceConfig.socketPath.c_str());
            auto instance = std::make_unique<ConsoleInstance>(
                io_context_, deviceConfig, sharedBus_,
                globalStopSource.get_token());

            auto ec = instance->start();
            if (ec)
            {
                LOG_ERROR("Failed to start console {}: {}", deviceConfig.name,
                          ec.message());
                continue;
            }

            instances_.push_back(std::move(instance));
            LOG_INFO("Console {} started successfully", deviceConfig.name);
        }

        if (instances_.empty())
        {
            LOG_ERROR("No console instances started");
            return boost::system::errc::make_error_code(
                boost::system::errc::no_such_device);
        }

        return boost::system::error_code{};
    }

    void stop()
    {
        LOG_INFO("Stopping all console instances");
        for (auto& instance : instances_)
        {
            instance->stop();
        }
        instances_.clear();
    }

  private:
    net::any_io_executor io_context_;
    ConsoleConfig config_;
    std::shared_ptr<sdbusplus::asio::connection> sharedBus_;
    std::vector<std::unique_ptr<ConsoleInstance>> instances_;
};
int main(int argc, const char* argv[])
{
    try
    {
        reactor::getLogger().setLogLevel(reactor::LogLevel::DEBUG);
        // Check for config file argument
        if (argc < 2)
        {
            LOG_ERROR("Usage: {} <config-file>", argv[0]);
            LOG_ERROR("Example: {} /etc/obmc-console-multi.conf", argv[0]);
            return EXIT_FAILURE;
        }

        std::string configFile = argv[1];

        // Setup signal handlers
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        // Load configuration from file
        auto consoleConfigOpt = ConsoleConfig::loadFromFile(configFile);
        if (!consoleConfigOpt)
        {
            LOG_ERROR("Failed to load configuration file: {}", configFile);
            return EXIT_FAILURE;
        }

        ConsoleConfig consoleConfig = *consoleConfigOpt;
        LOG_INFO("Loaded configuration from: {}", configFile);
        LOG_INFO("Log size: {}", consoleConfig.logsize);
        LOG_INFO("Log file: {}", consoleConfig.logfile);

        if (consoleConfig.devices.empty())
        {
            LOG_ERROR("No devices configured in {}", configFile);
            return EXIT_FAILURE;
        }

        // Create IO context
        net::io_context io_context;

        // Setup signal handling
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait(
            [&io_context](const boost::system::error_code& ec, int signal) {
                if (!ec)
                {
                    LOG_INFO("Received signal {}, stopping server...", signal);
                    globalStopSource.request_stop();
                    io_context.stop();
                }
            });

        // Create and start multi-console server
        MultiConsoleServer server(io_context.get_executor(), consoleConfig);

        auto ec = server.start();
        if (ec)
        {
            LOG_ERROR("Failed to start multi-console server: {}", ec.message());
            return EXIT_FAILURE;
        }

        LOG_INFO("Multi-console server running. Press Ctrl+C to stop.");

        // Run the IO context
        io_context.run();

        // Cleanup
        server.stop();
        LOG_INFO("Multi-console server stopped gracefully");

        return EXIT_SUCCESS;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return EXIT_FAILURE;
    }
}
