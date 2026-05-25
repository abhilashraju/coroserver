#pragma once
#include "beastdefs.hpp"
#include "logger.hpp"
#include "make_awaitable.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <boost/asio/posix/stream_descriptor.hpp>

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace NSNAME
{

/**
 * @brief UART configuration structure
 */
struct UartConfig
{
    std::string device;       // UART device path (e.g., "/dev/ttyS0")
    speed_t baudRate;         // Baud rate (e.g., B115200)
    bool rawMode;             // Enable raw mode (no processing)
    bool enableParity;        // Enable parity checking
    bool evenParity;          // Use even parity (if enabled)
    int stopBits;             // Number of stop bits (1 or 2)
    int dataBits;             // Data bits (5, 6, 7, or 8)
    bool hardwareFlowControl; // Enable hardware flow control (RTS/CTS)

    UartConfig() :
        device("/dev/ttyS0"), baudRate(B115200), rawMode(true),
        enableParity(false), evenParity(false), stopBits(1), dataBits(8),
        hardwareFlowControl(false)
    {}
};

/**
 * @brief UART device wrapper with async I/O support
 */
class UartDevice
{
  public:
    UartDevice(net::any_io_executor io_context, const UartConfig& config) :
        config_(config), stream_(io_context), fd_(-1)
    {}

    ~UartDevice()
    {
        close();
    }

    /**
     * @brief Open and configure the UART device
     * @return error_code indicating success or failure
     */
    boost::system::error_code open()
    {
        // Open the UART device
        fd_ = ::open(config_.device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0)
        {
            LOG_ERROR("Failed to open UART device: {}", config_.device);
            return boost::system::error_code(errno,
                                             boost::system::system_category());
        }

        // Configure termios
        struct termios tty;
        if (tcgetattr(fd_, &tty) != 0)
        {
            LOG_ERROR("Failed to get UART attributes");
            ::close(fd_);
            fd_ = -1;
            return boost::system::error_code(errno,
                                             boost::system::system_category());
        }

        // Set baud rate
        cfsetospeed(&tty, config_.baudRate);
        cfsetispeed(&tty, config_.baudRate);

        // Configure data bits
        tty.c_cflag &= ~CSIZE;
        switch (config_.dataBits)
        {
            case 5:
                tty.c_cflag |= CS5;
                break;
            case 6:
                tty.c_cflag |= CS6;
                break;
            case 7:
                tty.c_cflag |= CS7;
                break;
            case 8:
            default:
                tty.c_cflag |= CS8;
                break;
        }

        // Configure stop bits
        if (config_.stopBits == 2)
        {
            tty.c_cflag |= CSTOPB;
        }
        else
        {
            tty.c_cflag &= ~CSTOPB;
        }

        // Configure parity
        if (config_.enableParity)
        {
            tty.c_cflag |= PARENB;
            if (config_.evenParity)
            {
                tty.c_cflag &= ~PARODD;
            }
            else
            {
                tty.c_cflag |= PARODD;
            }
        }
        else
        {
            tty.c_cflag &= ~PARENB;
        }

        // Configure hardware flow control
        if (config_.hardwareFlowControl)
        {
            tty.c_cflag |= CRTSCTS;
        }
        else
        {
            tty.c_cflag &= ~CRTSCTS;
        }

        // Enable receiver and set local mode
        tty.c_cflag |= (CLOCAL | CREAD);

        // Configure raw mode
        if (config_.rawMode)
        {
            cfmakeraw(&tty);
        }

        // Apply settings
        if (tcsetattr(fd_, TCSANOW, &tty) != 0)
        {
            LOG_ERROR("Failed to set UART attributes");
            ::close(fd_);
            fd_ = -1;
            return boost::system::error_code(errno,
                                             boost::system::system_category());
        }

        // Assign the file descriptor to the stream
        stream_.assign(fd_);

        LOG_INFO("UART device opened: {} at baud rate {}", config_.device,
                 getBaudRateString(config_.baudRate));

        return boost::system::error_code{};
    }

    /**
     * @brief Close the UART device
     */
    void close()
    {
        if (fd_ != -1)
        {
            stream_.release();
            ::close(fd_);
            fd_ = -1;
            LOG_INFO("UART device closed: {}", config_.device);
        }
    }

    /**
     * @brief Async read from UART
     * @param buffer Buffer to read into
     * @return Pair of error_code and bytes read
     */
    net::awaitable<std::pair<boost::system::error_code, std::size_t>> read(
        net::mutable_buffer buffer)
    {
        boost::system::error_code ec;
        std::size_t bytes_transferred = co_await stream_.async_read_some(
            buffer, net::redirect_error(net::use_awaitable, ec));

        if (ec && ec != boost::asio::error::eof)
        {
            LOG_ERROR("Error reading from UART: {}", ec.message());
        }

        co_return std::make_pair(ec, bytes_transferred);
    }

    /**
     * @brief Async write to UART
     * @param buffer Buffer to write from
     * @return Pair of error_code and bytes written
     */
    net::awaitable<std::pair<boost::system::error_code, std::size_t>> write(
        net::const_buffer buffer)
    {
        boost::system::error_code ec;
        std::size_t bytes_transferred = co_await stream_.async_write_some(
            buffer, net::redirect_error(net::use_awaitable, ec));

        if (ec)
        {
            LOG_ERROR("Error writing to UART: {}", ec.message());
        }

        co_return std::make_pair(ec, bytes_transferred);
    }

    /**
     * @brief Check if UART device is open
     */
    bool isOpen() const
    {
        return fd_ != -1 && stream_.is_open();
    }

    /**
     * @brief Get the underlying stream descriptor
     */
    net::posix::stream_descriptor& getStream()
    {
        return stream_;
    }

    /**
     * @brief Configure VUART sysfs attribute
     * @param attrName Attribute name (e.g., "lpc_address", "sirq")
     * @param value Value to write
     * @return error_code indicating success or failure
     */
    boost::system::error_code configureSysfsAttribute(
        const std::string& attrName, uint16_t value)
    {
        std::string sysfsPath = findSysfsDeviceNode();
        if (sysfsPath.empty())
        {
            LOG_WARNING(
                "Cannot find sysfs device node for {}, skipping {} configuration",
                config_.device, attrName);
            return boost::system::error_code{};
        }

        std::string attrPath = sysfsPath + "/" + attrName;
        std::ofstream attrFile(attrPath);
        if (!attrFile.is_open())
        {
            LOG_WARNING("Cannot access sysfs attribute {} for device {}",
                        attrName, config_.device);
            return boost::system::error_code(errno,
                                             boost::system::system_category());
        }

        attrFile << "0x" << std::hex << value;
        if (!attrFile.good())
        {
            LOG_ERROR("Failed to write to sysfs attribute {} for device {}",
                      attrName, config_.device);
            return boost::system::error_code(errno,
                                             boost::system::system_category());
        }

        LOG_INFO("Configured {} = 0x{:x} for device {}", attrName, value,
                 config_.device);
        return boost::system::error_code{};
    }

  private:
    /**
     * @brief Find sysfs device node for the UART device
     * @return Path to sysfs device node, or empty string if not found
     */
    std::string findSysfsDeviceNode()
    {
        namespace fs = std::filesystem;

        // Extract device name from path (e.g., "ttyS0" from "/dev/ttyS0")
        std::string deviceName = fs::path(config_.device).filename().string();

        // Try to resolve symlink if it exists
        std::error_code ec;
        fs::path devicePath = fs::canonical(config_.device, ec);
        if (!ec)
        {
            deviceName = devicePath.filename().string();
        }

        // Build sysfs class path
        std::string sysfsClassPath = "/sys/class/tty/" + deviceName;
        fs::path classPath = fs::canonical(sysfsClassPath, ec);
        if (ec)
        {
            LOG_DEBUG("Cannot resolve sysfs path for {}: {}", deviceName,
                      ec.message());
            return "";
        }

        // Try both kernel 6.8+ and pre-6.8 directory structures
        const std::vector<std::string> relDirs = {"../../../../", "../../"};

        for (const auto& relDir : relDirs)
        {
            fs::path deviceNode = classPath / relDir;
            deviceNode = fs::canonical(deviceNode, ec);
            if (ec)
            {
                continue;
            }

            // Check if lpc_address exists (indicates VUART)
            fs::path lpcAddrPath = deviceNode / "lpc_address";
            if (fs::exists(lpcAddrPath))
            {
                LOG_DEBUG("Found VUART sysfs device node: {}",
                          deviceNode.string());
                return deviceNode.string();
            }
        }

        LOG_DEBUG("No VUART sysfs device node found for {}", deviceName);
        return "";
    }
    /**
     * @brief Convert baud rate constant to string
     */
    std::string getBaudRateString(speed_t baud) const
    {
        switch (baud)
        {
            case B50:
                return "50";
            case B75:
                return "75";
            case B110:
                return "110";
            case B134:
                return "134";
            case B150:
                return "150";
            case B200:
                return "200";
            case B300:
                return "300";
            case B600:
                return "600";
            case B1200:
                return "1200";
            case B1800:
                return "1800";
            case B2400:
                return "2400";
            case B4800:
                return "4800";
            case B9600:
                return "9600";
            case B19200:
                return "19200";
            case B38400:
                return "38400";
            case B57600:
                return "57600";
            case B115200:
                return "115200";
            case B230400:
                return "230400";
            default:
                return "unknown";
        }
    }

    UartConfig config_;
    net::posix::stream_descriptor stream_;
    int fd_;
};

/**
 * @brief UART Server that handles UART communication with a router pattern
 * @tparam Router Callable that processes UART data
 */
template <typename Router>
class UartServer
{
  public:
    UartServer(net::any_io_executor io_context, const UartConfig& config,
               Router& router) :
        context_(io_context), uart_(io_context, config), router_(router)
    {}

    /**
     * @brief Start the UART server
     * @return error_code indicating success or failure
     */
    boost::system::error_code start()
    {
        auto ec = uart_.open();
        if (ec)
        {
            return ec;
        }

        // Start the read loop
        boost::asio::co_spawn(context_, handleUart(), boost::asio::detached);

        return boost::system::error_code{};
    }

    /**
     * @brief Stop the UART server
     */
    void stop()
    {
        uart_.close();
    }

    /**
     * @brief Get reference to UART device
     */
    UartDevice& getUart()
    {
        return uart_;
    }

  private:
    /**
     * @brief Main UART handling coroutine
     */
    net::awaitable<void> handleUart()
    {
        LOG_INFO("UART server started");

        if constexpr (requires { router_(uart_); })
        {
            co_await router_(uart_);
        }
        else
        {
            LOG_ERROR("Router does not accept UartDevice reference");
        }

        LOG_INFO("UART server stopped");
    }

    net::any_io_executor context_;
    UartDevice uart_;
    Router& router_;
};

} // namespace NSNAME
