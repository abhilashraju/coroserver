/**
 * @file uart_server.cpp
 * @brief Example UART server using coroserver patterns
 *
 * This example demonstrates how to create a UART server that:
 * - Opens and configures a UART device
 * - Reads data asynchronously from the UART
 * - Echoes received data back to the UART
 * - Logs all communication
 *
 * Usage:
 *   uart_server --device /dev/ttyS0 --baud 115200
 */

#include "uart_server.hpp"

#include "command_line_parser.hpp"
#include "logger.hpp"

#include <csignal>
#include <iostream>

using namespace NSNAME;

// Global flag for graceful shutdown
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
 * @brief Parse baud rate string to termios constant
 */
speed_t parseBaudRate(std::string_view baudStr)
{
    int baud = std::stoi(std::string(baudStr));
    switch (baud)
    {
        case 50:
            return B50;
        case 75:
            return B75;
        case 110:
            return B110;
        case 134:
            return B134;
        case 150:
            return B150;
        case 200:
            return B200;
        case 300:
            return B300;
        case 600:
            return B600;
        case 1200:
            return B1200;
        case 1800:
            return B1800;
        case 2400:
            return B2400;
        case 4800:
            return B4800;
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
        // Parse command line arguments
        auto args = parseCommandline(argc, argv);
        auto [deviceOpt, baudOpt, parityOpt, stopBitsOpt, dataBitsOpt] =
            getArgs(args, "--device,-d", "--baud,-b", "--parity,-p",
                    "--stopbits,-s", "--databits,-D");

        // Setup signal handlers
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        // Configure UART
        UartConfig config;
        config.device = std::string(deviceOpt.value_or("/dev/ttyS0"));
        config.baudRate = parseBaudRate(baudOpt.value_or("115200"));
        config.rawMode = true;
        config.enableParity = parityOpt.has_value();
        config.evenParity = parityOpt.value_or("even") == "even";
        config.stopBits = std::stoi(std::string(stopBitsOpt.value_or("1")));
        config.dataBits = std::stoi(std::string(dataBitsOpt.value_or("8")));
        config.hardwareFlowControl = false;

        LOG_INFO("Starting UART server on device: {}", config.device);

        // Create IO context
        net::io_context io_context;

        // Setup signal handling to stop io_context
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

        // Define the router that handles UART communication
        auto router = [&io_context](UartDevice& uart) -> net::awaitable<void> {
            LOG_INFO("UART router started");

            std::array<char, 1024> buffer;

            while (!shutdownRequested && uart.isOpen())
            {
                // Read data from UART
                auto [ec, bytesRead] = co_await uart.read(net::buffer(buffer));

                if (ec)
                {
                    if (ec == boost::asio::error::operation_aborted)
                    {
                        LOG_INFO("UART read operation aborted");
                        break;
                    }
                    LOG_ERROR("Error reading from UART: {}", ec.message());
                    break;
                }

                if (bytesRead > 0)
                {
                    // Log received data
                    std::string received(buffer.data(), bytesRead);
                    LOG_INFO("Received {} bytes: {}", bytesRead, received);

                    // Echo back the data
                    auto [writeEc, bytesWritten] = co_await uart.write(
                        net::buffer(buffer.data(), bytesRead));

                    if (writeEc)
                    {
                        LOG_ERROR("Error writing to UART: {}",
                                  writeEc.message());
                        break;
                    }

                    LOG_INFO("Echoed {} bytes back", bytesWritten);
                }
            }

            LOG_INFO("UART router stopped");
        };

        // Create and start UART server
        UartServer server(io_context.get_executor(), config, router);

        auto ec = server.start();
        if (ec)
        {
            LOG_ERROR("Failed to start UART server: {}", ec.message());
            return EXIT_FAILURE;
        }

        LOG_INFO("UART server running. Press Ctrl+C to stop.");

        // Run the IO context
        io_context.run();

        // Cleanup
        server.stop();
        LOG_INFO("UART server stopped gracefully");

        return EXIT_SUCCESS;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return EXIT_FAILURE;
    }
}
