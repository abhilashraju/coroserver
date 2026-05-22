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
#include "socket_streams.hpp"

#include <sys/socket.h>
#include <termios.h>

#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
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

/**
 * @brief Socket pair consumer for D-Bus clients
 *
 * Creates a Unix domain socket pair and manages one end as a console consumer,
 * similar to dbus_create_socket_consumer in the C implementation.
 */
class SocketPairConsumer
{
  public:
    SocketPairConsumer(net::any_io_executor io_context) :
        io_context_(io_context),
        serverSocket_(
            std::make_shared<net::posix::stream_descriptor>(io_context)),
        timer_(std::make_shared<net::steady_timer>(io_context))
    {}

    ~SocketPairConsumer()
    {
        LOG_DEBUG("Closing sock pair {}", serverSocket_->native_handle());
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
            serverSocket_->assign(fds[0]);
            // Return client FD - caller takes ownership
            return fds[1];
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to assign socket: {}", e.what());
            ::close(fds[0]);
            ::close(fds[1]);
            return -1;
        }
    }

    bool isOpen() const
    {
        return serverSocket_->is_open();
    }

    /**
     * @brief Get TimedStreamer wrapper for this consumer
     */
    auto getTimedStreamer()
    {
        return TimedStreamer<net::posix::stream_descriptor>{serverSocket_,
                                                            timer_};
    }

  private:
    void close()
    {
        if (serverSocket_->is_open())
        {
            boost::system::error_code ec;
            serverSocket_->close(ec);
        }
    }

    net::any_io_executor io_context_;
    std::shared_ptr<net::posix::stream_descriptor> serverSocket_;
    std::shared_ptr<net::steady_timer> timer_;
};

/**
 * @brief D-Bus interface handler for console access
 *
 * Implements xyz.openbmc_project.Console.Access and
 * xyz.openbmc_project.Console.UART interfaces.
 */
// Forward declaration
template <typename StreamType>
struct TimedStreamer;

template <typename RouterType>
class ConsoleDbusInterface
{
  public:
    ConsoleDbusInterface(
        net::any_io_executor io_context,
        std::shared_ptr<sdbusplus::asio::connection> bus,
        const std::string& consoleId, std::function<speed_t()> getBaud,
        std::function<void(speed_t)> setBaud, RouterType& router) :
        io_context_(io_context), bus_(bus), consoleId_(consoleId),
        getBaud_(std::move(getBaud)), setBaud_(std::move(setBaud)),
        router_(router)
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

            // Register Connect method
            accessInterface_->register_method(
                "Connect", [this]() -> sdbusplus::message::unix_fd {
                    return handleConnect();
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

  private:
    /**
     * @brief Handle D-Bus Connect method
     * Creates socket pair and returns client FD
     */
    sdbusplus::message::unix_fd handleConnect()
    {
        try
        {
            auto consumer = std::make_shared<SocketPairConsumer>(io_context_);

            int clientFd = consumer->createSocketPair();
            if (clientFd < 0)
            {
                throw std::runtime_error("Failed to create socket pair");
            }

            // Route D-Bus consumer through router's operator()
            // This treats D-Bus clients exactly like Unix socket clients
            // Lambda captures consumer ownership and automatically cleans up
            // when router returns
            auto timedStreamer = consumer->getTimedStreamer();
            boost::asio::co_spawn(
                io_context_,
                [consumer, timedStreamer, clientFd,
                 this]() -> boost::asio::awaitable<void> {
                    co_await router_(timedStreamer, clientFd);
                    // consumer is automatically destroyed here when coroutine
                    // completes
                },
                boost::asio::detached);

            LOG_INFO("D-Bus client connected via router");

            // Return file descriptor - sdbusplus will duplicate it for transfer
            return sdbusplus::message::unix_fd(clientFd);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Connect method failed: {}", e.what());
            throw;
        }
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
    std::function<speed_t()> getBaud_;
    std::function<void(speed_t)> setBaud_;
    RouterType& router_;

    std::unique_ptr<sdbusplus::asio::object_server> objectServer_;
    std::shared_ptr<sdbusplus::asio::dbus_interface> accessInterface_;
    std::shared_ptr<sdbusplus::asio::dbus_interface> uartInterface_;
};

} // namespace NSNAME
