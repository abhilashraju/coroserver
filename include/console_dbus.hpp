/**
 * @file console_dbus.hpp
 * @brief D-Bus interface for OBMC Console Server
 *
 * Implements the xyz.openbmc_project.Console.Access and
 * xyz.openbmc_project.Console.UART D-Bus interfaces for console access.
 */

#pragma once

#include "beastdefs.hpp"
#include "logger.hpp"

#include <sys/socket.h>
#include <termios.h>

#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/circular_buffer.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/asio/property.hpp>

#include <cerrno>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace NSNAME
{

using unix_socket = boost::asio::local::stream_protocol;
using RingBuffer = boost::circular_buffer<char>;

/**
 * @brief Socket pair consumer for D-Bus clients
 *
 * Creates a Unix domain socket pair and manages one end as a console consumer,
 * similar to dbus_create_socket_consumer in the C implementation.
 */
class SocketPairConsumer
{
  public:
    SocketPairConsumer(
        net::any_io_executor io_context, RingBuffer& ringBuffer,
        std::function<void(const std::vector<char>&)> uartWriter) :
        io_context_(io_context), ringBuffer_(ringBuffer),
        uartWriter_(std::move(uartWriter)), serverSocket_(io_context),
        clientFd_(-1)
    {}

    ~SocketPairConsumer()
    {
        close();
    }

    /**
     * @brief Create socket pair and return client FD
     * @return File descriptor for client end, or -1 on error
     */
    int createSocketPair()
    {
        int fds[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)
        {
            LOG_ERROR("Failed to create socket pair: {}", strerror(errno));
            return -1;
        }

        // fds[0] - server end (we manage)
        // fds[1] - client end (returned to D-Bus caller)
        try
        {
            serverSocket_.assign(fds[0]);
            clientFd_ = fds[1];

            // Send console history to new client
            sendHistory();

            // Start reading from client
            boost::asio::co_spawn(io_context_, handleClient(),
                                  boost::asio::detached);

            return clientFd_;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to assign socket: {}", e.what());
            ::close(fds[0]);
            ::close(fds[1]);
            return -1;
        }
    }

    /**
     * @brief Write data to this consumer (from UART)
     */
    net::awaitable<void> write(const std::vector<char>& data)
    {
        if (!serverSocket_.is_open())
        {
            co_return;
        }

        try
        {
            co_await net::async_write(serverSocket_, net::buffer(data),
                                      net::use_awaitable);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to write to socket pair consumer: {}", e.what());
        }
    }

    bool isOpen() const
    {
        return serverSocket_.is_open();
    }

  private:
    void sendHistory()
    {
        if (ringBuffer_.empty() || !serverSocket_.is_open())
        {
            return;
        }

        std::vector<char> history(ringBuffer_.begin(), ringBuffer_.end());
        boost::asio::co_spawn(
            io_context_,
            [this, history]() -> net::awaitable<void> {
                try
                {
                    co_await net::async_write(serverSocket_,
                                              net::buffer(history),
                                              net::use_awaitable);
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("Failed to send history: {}", e.what());
                }
            },
            boost::asio::detached);
    }

    net::awaitable<void> handleClient()
    {
        std::array<char, 1024> buffer;

        try
        {
            while (serverSocket_.is_open())
            {
                size_t bytesRead = co_await serverSocket_.async_read_some(
                    net::buffer(buffer), net::use_awaitable);

                if (bytesRead > 0 && uartWriter_)
                {
                    std::vector<char> data(buffer.begin(),
                                           buffer.begin() + bytesRead);
                    uartWriter_(data);
                }
            }
        }
        catch (const boost::system::system_error& e)
        {
            if (e.code() != net::error::eof &&
                e.code() != net::error::operation_aborted)
            {
                LOG_ERROR("Socket pair consumer read error: {}", e.what());
            }
        }

        close();
    }

    void close()
    {
        if (serverSocket_.is_open())
        {
            boost::system::error_code ec;
            serverSocket_.close(ec);
        }
        if (clientFd_ >= 0)
        {
            // Client FD is passed to D-Bus caller, they own it
            clientFd_ = -1;
        }
    }

    net::any_io_executor io_context_;
    RingBuffer& ringBuffer_;
    std::function<void(const std::vector<char>&)> uartWriter_;
    net::posix::stream_descriptor serverSocket_;
    int clientFd_;
};

/**
 * @brief D-Bus interface handler for console access
 *
 * Implements xyz.openbmc_project.Console.Access and
 * xyz.openbmc_project.Console.UART interfaces.
 */
class ConsoleDbusInterface
{
  public:
    ConsoleDbusInterface(
        net::any_io_executor io_context,
        std::shared_ptr<sdbusplus::asio::connection> bus,
        const std::string& consoleId, RingBuffer& ringBuffer,
        std::function<void(const std::vector<char>&)> uartWriter,
        std::function<speed_t()> getBaud,
        std::function<void(speed_t)> setBaud) :
        io_context_(io_context), bus_(bus), consoleId_(consoleId),
        ringBuffer_(ringBuffer), uartWriter_(std::move(uartWriter)),
        getBaud_(std::move(getBaud)), setBaud_(std::move(setBaud))
    {}

    /**
     * @brief Initialize D-Bus interfaces
     */
    bool initialize()
    {
        try
        {
            std::string busName = "xyz.openbmc_project.Console." + consoleId_;
            std::string objectPath =
                "/xyz/openbmc_project/console/" + consoleId_;

            // Request bus name
            bus_->request_name(busName.c_str());

            // Create object server
            objectServer_ =
                std::make_unique<sdbusplus::asio::object_server>(bus_);

            // Add Access interface
            accessInterface_ = objectServer_->add_interface(
                objectPath, "xyz.openbmc_project.Console.Access");

            // Register Connect method that returns a file descriptor
            accessInterface_->register_method("Connect", [this]() -> int {
                int fd = handleConnect();
                // Close our copy after returning
                int dupFd = ::dup(fd);
                ::close(fd);
                return dupFd;
            });

            accessInterface_->initialize();

            // Add UART interface
            uartInterface_ = objectServer_->add_interface(
                objectPath, "xyz.openbmc_project.Console.UART");

            // Register Baud property
            uartInterface_->register_property(
                "Baud", uint64_t(0),
                [this](const uint64_t& value, uint64_t& old) {
                    return handleSetBaud(value, old);
                },
                [this](const uint64_t& value) { return handleGetBaud(); });

            uartInterface_->initialize();

            LOG_INFO("D-Bus interfaces initialized: {}", busName);
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to initialize D-Bus interfaces: {}", e.what());
            return false;
        }
    }

    /**
     * @brief Broadcast data to all socket pair consumers
     */
    net::awaitable<void> broadcastToConsumers(const std::vector<char>& data)
    {
        // Remove closed consumers
        consumers_.erase(std::remove_if(consumers_.begin(), consumers_.end(),
                                        [](const auto& consumer) {
                                            return !consumer->isOpen();
                                        }),
                         consumers_.end());

        // Broadcast to all active consumers
        for (auto& consumer : consumers_)
        {
            boost::asio::co_spawn(io_context_, consumer->write(data),
                                  boost::asio::detached);
        }
        co_return;
    }

  private:
    /**
     * @brief Handle D-Bus Connect method
     * Creates socket pair and returns client FD
     */
    int handleConnect()
    {
        auto consumer = std::make_shared<SocketPairConsumer>(
            io_context_, ringBuffer_, uartWriter_);

        int clientFd = consumer->createSocketPair();
        if (clientFd < 0)
        {
            throw std::runtime_error("Failed to create socket pair");
        }

        consumers_.push_back(consumer);

        LOG_INFO("D-Bus client connected, total consumers: {}",
                 consumers_.size());

        // Return the client FD - sdbusplus will handle the transfer
        // and we'll close our copy after it's sent
        int fdToReturn = clientFd;

        // Note: The FD will be closed by the caller after sending
        return fdToReturn;
    }

    /**
     * @brief Handle Baud property getter
     */
    uint64_t handleGetBaud()
    {
        if (!getBaud_)
        {
            return 115200; // Default
        }

        speed_t baud = getBaud_();
        return baudToInt(baud);
    }

    /**
     * @brief Handle Baud property setter
     */
    bool handleSetBaud(const uint64_t& value, uint64_t& old)
    {
        if (!setBaud_)
        {
            return false;
        }

        speed_t baud = intToBaud(value);
        if (baud == B0)
        {
            LOG_WARNING("Invalid baud rate: {}", value);
            return false;
        }

        old = handleGetBaud();
        setBaud_(baud);

        LOG_INFO("Baud rate changed: {} -> {}", old, value);
        return true;
    }

    /**
     * @brief Convert termios baud constant to integer
     */
    static uint64_t baudToInt(speed_t baud)
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

    /**
     * @brief Convert integer to termios baud constant
     */
    static speed_t intToBaud(uint64_t value)
    {
        switch (value)
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
                return B0; // Invalid
        }
    }

    net::any_io_executor io_context_;
    std::shared_ptr<sdbusplus::asio::connection> bus_;
    std::string consoleId_;
    RingBuffer& ringBuffer_;
    std::function<void(const std::vector<char>&)> uartWriter_;
    std::function<speed_t()> getBaud_;
    std::function<void(speed_t)> setBaud_;

    std::unique_ptr<sdbusplus::asio::object_server> objectServer_;
    std::shared_ptr<sdbusplus::asio::dbus_interface> accessInterface_;
    std::shared_ptr<sdbusplus::asio::dbus_interface> uartInterface_;

    std::vector<std::shared_ptr<SocketPairConsumer>> consumers_;
};

} // namespace NSNAME
