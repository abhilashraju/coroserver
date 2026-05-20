/**
 * @file console_server.cpp
 * @brief OBMC Console Server - Bridges UART device to Unix socket clients
 *
 * This server implements functionality similar to obmc-console-server:
 * - Opens and manages a UART/serial device
 * - Creates a Unix domain socket for client connections
 * - Multiplexes UART data to multiple connected clients
 * - Forwards client input to the UART device
 * - Maintains a ringbuffer for console history
 *
 * Usage:
 *   console_server --device /dev/ttyS0 --socket /tmp/console.sock --baud 115200
 */

#include "command_line_parser.hpp"
#include "console_config.hpp"
#include "console_dbus.hpp"
#include "logger.hpp"
#include "tcp_server.hpp"
#include "uart_server.hpp"

#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/circular_buffer.hpp>
#include <sdbusplus/asio/connection.hpp>

#include <csignal>
#include <memory>
#include <set>
#include <vector>

using namespace NSNAME;
using unix_socket = boost::asio::local::stream_protocol;
using ssl_socket = boost::asio::ssl::stream<unix_socket::socket>;

// Global shutdown flag
std::atomic<bool> shutdownRequested{false};

void signalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        LOG_INFO("Shutdown signal received");
        shutdownRequested = true;
    }
}

/**
 * @brief Type alias for console history buffer
 */
using RingBuffer = boost::circular_buffer<char>;

/**
 * @brief Console router that handles client connections
 */
class ConsoleRouter
{
  public:
    ConsoleRouter(net::any_io_executor io_context, RingBuffer& ringBuffer,
                  std::vector<TimedStreamer<ssl_socket>>& clients,
                  std::unique_ptr<UartDevice>& uart) :
        io_context_(io_context), ringBuffer_(ringBuffer), clients_(clients),
        uart_(uart)
    {}

    net::awaitable<void> operator()(TimedStreamer<ssl_socket> streamer)
    {
        clients_.push_back(streamer);
        auto& myStreamer = clients_.back();
        static std::atomic<int> clientId{1};
        int myId = clientId++;
        LOG_INFO("Client {} connected", myId);

        // Send console history to new client
        if (!ringBuffer_.empty())
        {
            std::vector<char> history(ringBuffer_.begin(), ringBuffer_.end());
            auto [ec, bytes] =
                co_await myStreamer.write(boost::asio::buffer(history));
            if (ec)
            {
                LOG_ERROR("Failed to send history to client {}: {}", myId,
                          ec.message());
            }
        }

        // Read from client and forward to UART
        std::array<char, 1024> buffer;
        while (!shutdownRequested)
        {
            auto [ec, bytesRead] =
                co_await myStreamer.read(boost::asio::buffer(buffer));

            if (ec)
            {
                if (ec != boost::asio::error::eof)
                {
                    LOG_ERROR("Client {} read error: {}", myId, ec.message());
                }
                break;
            }

            if (bytesRead > 0 && uart_ && uart_->isOpen())
            {
                std::vector<char> data(buffer.begin(),
                                       buffer.begin() + bytesRead);
                boost::asio::co_spawn(
                    io_context_,
                    [this, data]() -> net::awaitable<void> {
                        co_await uart_->write(boost::asio::buffer(data));
                    },
                    boost::asio::detached);
            }
        }

        // Remove this client from the list
        clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                      [&myStreamer](const auto& client) {
                                          return &client == &myStreamer;
                                      }),
                       clients_.end());
        LOG_INFO("Client {} disconnected", myId);
    }

  private:
    net::any_io_executor io_context_;
    RingBuffer& ringBuffer_;
    std::vector<TimedStreamer<ssl_socket>>& clients_;
    std::unique_ptr<UartDevice>& uart_;
};

/**
 * @brief Console server that manages UART and client connections
 */
class ConsoleServer
{
  public:
    ConsoleServer(net::any_io_executor io_context, const UartConfig& uartConfig,
                  const std::string& socketPath, const std::string& consoleId,
                  boost::asio::ssl::context& sslContext,
                  std::shared_ptr<sdbusplus::asio::connection> bus = nullptr) :
        io_context_(io_context), uartConfig_(uartConfig),
        socketPath_(socketPath), consoleId_(consoleId),
        acceptor_(io_context, socketPath, sslContext),
        ringBuffer_(128 * 1024), // 128KB buffer like obmc-console
        router_(io_context, ringBuffer_, clients_, uart_),
        server_(io_context, acceptor_, router_), bus_(bus)
    {}

    boost::system::error_code start()
    {
        // Remove existing socket file
        ::unlink(socketPath_.c_str());

        LOG_INFO("Unix socket created: {}", socketPath_);

        // Initialize D-Bus interface if bus connection provided
        if (bus_)
        {
            initializeDbusInterface();
        }

        // Start UART handler
        boost::asio::co_spawn(io_context_, handleUart(), boost::asio::detached);

        return boost::system::error_code{};
    }

    void stop()
    {
        ::unlink(socketPath_.c_str());
    }

  private:
    net::awaitable<void> handleUart()
    {
        uart_ = std::make_unique<UartDevice>(io_context_, uartConfig_);

        auto ec = uart_->open();
        if (ec)
        {
            LOG_ERROR("Failed to open UART device: {}", ec.message());
            co_return;
        }

        LOG_INFO("UART device opened, starting console server");

        std::array<char, 1024> buffer;

        while (!shutdownRequested && uart_->isOpen())
        {
            // Read from UART
            auto [readEc, bytesRead] =
                co_await uart_->read(boost::asio::buffer(buffer));

            if (readEc)
            {
                if (readEc != boost::asio::error::operation_aborted)
                {
                    LOG_ERROR("UART read error: {}", readEc.message());
                }
                break;
            }

            if (bytesRead > 0)
            {
                std::vector<char> data(buffer.begin(),
                                       buffer.begin() + bytesRead);

                // Add to ringbuffer
                ringBuffer_.insert(ringBuffer_.end(), data.begin(), data.end());

                // Broadcast to all Unix socket clients
                for (auto& client : clients_)
                {
                    boost::asio::co_spawn(
                        io_context_,
                        [&client, data]() -> net::awaitable<void> {
                            auto [ec, bytes] = co_await client.write(
                                boost::asio::buffer(data));
                            if (ec)
                            {
                                LOG_ERROR("Failed to write to client: {}",
                                          ec.message());
                            }
                        },
                        boost::asio::detached);
                }

                // Broadcast to D-Bus socket pair consumers
                if (dbusInterface_)
                {
                    boost::asio::co_spawn(
                        io_context_, dbusInterface_->broadcastToConsumers(data),
                        boost::asio::detached);
                }
            }
        }

        LOG_INFO("UART handler stopped");
    }

    void initializeDbusInterface()
    {
        auto uartWriter = [this](const std::vector<char>& data) {
            if (uart_ && uart_->isOpen())
            {
                boost::asio::co_spawn(
                    io_context_,
                    [this, data]() -> net::awaitable<void> {
                        co_await uart_->write(boost::asio::buffer(data));
                    },
                    boost::asio::detached);
            }
        };

        auto getBaud = [this]() -> speed_t { return uartConfig_.baudRate; };

        auto setBaud = [this](speed_t baud) {
            uartConfig_.baudRate = baud;
            if (uart_ && uart_->isOpen())
            {
                // Reconfigure UART with new baud rate
                uart_->close();
                auto ec = uart_->open();
                if (ec)
                {
                    LOG_ERROR("Failed to reopen UART with new baud rate: {}",
                              ec.message());
                }
            }
        };

        dbusInterface_ = std::make_unique<ConsoleDbusInterface>(
            io_context_, bus_, consoleId_, getBaud, setBaud, uartWriter);

        if (!dbusInterface_->initialize())
        {
            LOG_ERROR("Failed to initialize D-Bus interface");
            dbusInterface_.reset();
        }
    }

    net::any_io_executor io_context_;
    UartConfig uartConfig_;
    std::string socketPath_;
    std::string consoleId_;
    UnixStreamType acceptor_;
    RingBuffer ringBuffer_;
    std::unique_ptr<UartDevice> uart_;
    std::vector<TimedStreamer<ssl_socket>> clients_;
    ConsoleRouter router_;
    TcpServer<UnixStreamType, ConsoleRouter> server_;
    std::shared_ptr<sdbusplus::asio::connection> bus_;
    std::unique_ptr<ConsoleDbusInterface> dbusInterface_;
};

/**
 * @brief Parse baud rate string to termios constant
 */
speed_t parseBaudRate(std::string_view baudStr)
{
    int baud = std::stoi(std::string(baudStr));
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

int main(int argc, const char* argv[])
{
    try
    {
        // Check for config file argument
        if (argc < 2)
        {
            LOG_ERROR("Usage: {} <config-file> [console-id]", argv[0]);
            LOG_ERROR("Example: {} /etc/obmc-console.conf default", argv[0]);
            return EXIT_FAILURE;
        }

        std::string configFile = argv[1];
        std::string consoleId = (argc >= 3) ? argv[2] : "default";

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

        // Configure UART
        UartConfig uartConfig;
        uartConfig.device = consoleConfig.device;
        uartConfig.baudRate =
            parseBaudRate(std::to_string(consoleConfig.localTtyBaud));
        uartConfig.rawMode = true;
        uartConfig.enableParity = false;
        uartConfig.stopBits = 1;
        uartConfig.dataBits = 8;
        uartConfig.hardwareFlowControl = false;

        LOG_INFO("Starting console server");
        LOG_INFO("  UART device: {}", uartConfig.device);
        LOG_INFO("  Unix socket: {}", consoleConfig.socketPath);
        LOG_INFO("  Baud rate: {}", consoleConfig.localTtyBaud);
        LOG_INFO("  Log size: {}", consoleConfig.logsize);
        LOG_INFO("  Log file: {}", consoleConfig.logfile);

        // Create IO context
        net::io_context io_context;

        // Create D-Bus connection
        auto bus = std::make_shared<sdbusplus::asio::connection>(io_context);
        LOG_INFO("D-Bus connection established");

        // Create SSL context
        boost::asio::ssl::context sslContext(
            boost::asio::ssl::context::sslv23_server);

        // Setup signal handling
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait(
            [&io_context](const boost::system::error_code& ec, int signal) {
                if (!ec)
                {
                    LOG_INFO("Received signal {}, stopping server...", signal);
                    shutdownRequested = true;
                    io_context.stop();
                }
            });

        LOG_INFO("  Console ID: {}", consoleId);

        // Create and start console server with D-Bus support
        ConsoleServer server(io_context.get_executor(), uartConfig,
                             consoleConfig.socketPath, consoleId, sslContext,
                             bus);

        auto ec = server.start();
        if (ec)
        {
            LOG_ERROR("Failed to start console server: {}", ec.message());
            return EXIT_FAILURE;
        }

        LOG_INFO("Console server running. Press Ctrl+C to stop.");

        // Run the IO context
        io_context.run();

        // Cleanup
        server.stop();
        LOG_INFO("Console server stopped gracefully");

        return EXIT_SUCCESS;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return EXIT_FAILURE;
    }
}
