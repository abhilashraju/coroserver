#pragma once

#include <sys/stat.h>

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// Forward declarations for device types
namespace NSNAME
{
class UartDevice;
class PtyDevice;
class SshPtyDevice;
} // namespace NSNAME

namespace NSNAME
{

/**
 * @brief Base configuration common to all device types
 */
struct BaseDeviceConfig
{
    std::string name;         // Console identifier (e.g., "host", "bmc")
    std::string device;       // Device path (e.g., "/dev/ttyS0", "/dev/ptmx")
    std::string socketPath;   // Unix socket path
    int baudRate = 115200;    // Baud rate (for UART/VUART)
    mode_t socketMode = 0666; // Socket file permissions (default: rw-rw-rw-)
    std::string socketGroup;  // Socket group ownership (optional)
};

/**
 * @brief UART-specific configuration (console config)
 */
struct ConsoleUartConfig
{
    // No additional fields beyond base config
    static constexpr const char* toString()
    {
        return "UART";
    }

    template <typename ConsoleInstance>
    auto handle(ConsoleInstance* instance) const
    {
        return instance->initUartDevice();
    }
};

/**
 * @brief VUART-specific configuration (console config)
 */
struct ConsoleVuartConfig
{
    std::string lpcAddress = "0x3f8";
    int sirq = 4;
    static constexpr const char* toString()
    {
        return "VUART";
    }

    template <typename ConsoleInstance>
    auto handle(ConsoleInstance* instance) const
    {
        return instance->initUartDevice();
    }
};

/**
 * @brief PTY-specific configuration (console config)
 */
struct ConsolePtyConfig
{
    std::string shellPath = "/bin/sh";  // Shell executable path
    std::vector<std::string> shellArgs; // Shell arguments (e.g., "-i")
    static constexpr const char* toString()
    {
        return "PTY";
    }

    template <typename ConsoleInstance>
    auto handle(ConsoleInstance* instance) const
    {
        return instance->initPtyDevice();
    }
};

/**
 * @brief SSH PTY-specific configuration (console config)
 */
struct ConsoleSshPtyConfig
{
    std::string remoteHost;                // Remote host IP/hostname
    std::string remoteUser;                // SSH username
    std::string remotePassword;            // SSH password (optional)
    std::string sshKeyPath;                // SSH private key path
    int remotePort = 22;                   // SSH port
    std::string remoteShell = "/bin/bash"; // Shell to run on remote
    int connectTimeout = 30;               // SSH connection timeout
    static constexpr const char* toString()
    {
        return "SSH_PTY";
    }

    template <typename ConsoleInstance>
    auto handle(ConsoleInstance* instance) const
    {
        return instance->initSshPtyDevice();
    }
};

/**
 * @brief Multiplexing configuration (optional for any device)
 */
struct MuxConfig
{
    bool enabled = false;
    int muxIndex = 0;
    std::vector<std::string> muxGpios; // GPIO names for multiplexing
};

/**
 * @brief Device-specific configuration variant
 */
using DeviceSpecificConfig =
    std::variant<ConsoleUartConfig, ConsoleVuartConfig, ConsolePtyConfig,
                 ConsoleSshPtyConfig>;

/**
 * @brief Complete device configuration
 */
struct DeviceConfig
{
    BaseDeviceConfig base;
    DeviceSpecificConfig specific;
    MuxConfig mux;

    // Helper methods to access specific configs using std::visit pattern
    const ConsoleUartConfig* getUartConfig() const
    {
        return std::get_if<ConsoleUartConfig>(&specific);
    }

    const ConsoleVuartConfig* getVuartConfig() const
    {
        return std::get_if<ConsoleVuartConfig>(&specific);
    }

    const ConsolePtyConfig* getPtyConfig() const
    {
        return std::get_if<ConsolePtyConfig>(&specific);
    }

    const ConsoleSshPtyConfig* getSshPtyConfig() const
    {
        return std::get_if<ConsoleSshPtyConfig>(&specific);
    }

    // Convenience accessors for base config
    const std::string& getName() const
    {
        return base.name;
    }
    const std::string& getDevice() const
    {
        return base.device;
    }
    const std::string& getSocketPath() const
    {
        return base.socketPath;
    }
    int getBaudRate() const
    {
        return base.baudRate;
    }

    // Get device type string using visitor pattern
    std::string getDeviceTypeString() const
    {
        return std::visit(
            [](const auto& config) -> std::string { return config.toString(); },
            specific);
    }

    // Visitor pattern helper for type-safe device handling
    template <typename Visitor>
    auto visit(Visitor&& visitor) const
    {
        return std::visit(std::forward<Visitor>(visitor), specific);
    }

    template <typename Visitor>
    auto visit(Visitor&& visitor)
    {
        return std::visit(std::forward<Visitor>(visitor), specific);
    }
};

/**
 * @brief Configuration for console server
 */
struct ConsoleConfig
{
    std::string logsize = "256k";
    std::string logfile = "/var/log/obmc-console.log";
    std::vector<DeviceConfig> devices;

    // Default device for backward compatibility
    std::string defaultDevice = "/dev/ttyS0";
    std::string defaultSocketPath = "/tmp/obmc-console.sock";
    int defaultBaudRate = 115200;

    /**
     * @brief Parse logsize string to bytes
     */
    size_t getLogsizeBytes() const
    {
        size_t value = 0;
        char unit = 0;
        std::istringstream iss(logsize);
        iss >> value >> unit;

        switch (unit)
        {
            case 'k':
            case 'K':
                return value * 1024;
            case 'm':
            case 'M':
                return value * 1024 * 1024;
            case 'g':
            case 'G':
                return value * 1024 * 1024 * 1024;
            default:
                return value; // Assume bytes if no unit
        }
    }

    /**
     * @brief Get device configuration by name
     */
    std::optional<DeviceConfig> getDevice(const std::string& name) const
    {
        for (const auto& dev : devices)
        {
            if (dev.getName() == name)
            {
                return dev;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Create device-specific config based on type string
     */
    static DeviceSpecificConfig createSpecificConfig(
        const std::string& typeStr,
        const std::unordered_map<std::string, std::string>& values)
    {
        if (typeStr == "vuart" || typeStr == "VUART")
        {
            ConsoleVuartConfig vuart;
            if (values.count("lpc-address"))
            {
                vuart.lpcAddress = values.at("lpc-address");
            }
            if (values.count("sirq"))
            {
                vuart.sirq = std::stoi(values.at("sirq"));
            }
            return vuart;
        }
        else if (typeStr == "pty" || typeStr == "PTY")
        {
            ConsolePtyConfig pty;
            if (values.count("shell-path"))
            {
                pty.shellPath = values.at("shell-path");
            }
            if (values.count("shell-args"))
            {
                std::string args = values.at("shell-args");
                std::istringstream iss(args);
                std::string arg;
                while (iss >> arg)
                {
                    pty.shellArgs.push_back(arg);
                }
            }
            return pty;
        }
        else if (typeStr == "ssh-pty" || typeStr == "SSH-PTY" ||
                 typeStr == "SSH_PTY")
        {
            ConsoleSshPtyConfig sshPty;
            if (values.count("remote-host"))
            {
                sshPty.remoteHost = values.at("remote-host");
            }
            if (values.count("remote-user"))
            {
                sshPty.remoteUser = values.at("remote-user");
            }
            if (values.count("remote-password"))
            {
                sshPty.remotePassword = values.at("remote-password");
            }
            if (values.count("ssh-key-path"))
            {
                sshPty.sshKeyPath = values.at("ssh-key-path");
            }
            if (values.count("remote-port"))
            {
                sshPty.remotePort = std::stoi(values.at("remote-port"));
            }
            if (values.count("remote-shell"))
            {
                sshPty.remoteShell = values.at("remote-shell");
            }
            if (values.count("connect-timeout"))
            {
                sshPty.connectTimeout = std::stoi(values.at("connect-timeout"));
            }
            return sshPty;
        }

        // Default to UART
        return ConsoleUartConfig{};
    }

    /**
     * @brief Create mux config from values
     */
    static MuxConfig createMuxConfig(
        const std::unordered_map<std::string, std::string>& values)
    {
        MuxConfig mux;

        if (values.count("mux-index"))
        {
            mux.enabled = true;
            mux.muxIndex = std::stoi(values.at("mux-index"));
        }

        if (values.count("mux-gpios"))
        {
            mux.enabled = true;
            std::string gpios = values.at("mux-gpios");
            std::istringstream iss(gpios);
            std::string gpio;
            while (std::getline(iss, gpio, ','))
            {
                // Trim whitespace
                gpio.erase(0, gpio.find_first_not_of(" \t"));
                gpio.erase(gpio.find_last_not_of(" \t") + 1);
                mux.muxGpios.push_back(gpio);
            }
        }

        return mux;
    }

    /**
     * @brief Load configuration from file
     *
     * Supports both simple single-device config and multi-device sections:
     *
     * Simple format (backward compatible):
     *   device = /dev/ttyS0
     *   socket-path = /tmp/obmc-console.sock
     *   local-tty-baud = 115200
     *
     * Multi-device format:
     *   [console.host]
     *   device = /dev/ttyS0
     *   device-type = uart
     *   socket-path = /tmp/obmc-console-host.sock
     *   local-tty-baud = 115200
     *
     *   [console.remote]
     *   device = /dev/ptmx
     *   device-type = ssh-pty
     *   socket-path = /tmp/obmc-console-remote.sock
     *   remote-host = 9.6.28.100
     *   remote-user = root
     *   ssh-key-path = /root/.ssh/id_rsa
     */
    static std::optional<ConsoleConfig> loadFromFile(
        const std::string& configPath)
    {
        std::ifstream file(configPath);
        if (!file.is_open())
        {
            return std::nullopt;
        }

        ConsoleConfig config;
        std::string line;
        std::unordered_map<std::string, std::string> globalValues;
        std::unordered_map<std::string,
                           std::unordered_map<std::string, std::string>>
            sections;
        std::string currentSection;

        // Parse file
        while (std::getline(file, line))
        {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#')
            {
                continue;
            }

            // Check for section header [console.name]
            if (line[0] == '[' && line.back() == ']')
            {
                currentSection = line.substr(1, line.length() - 2);
                continue;
            }

            // Parse key = value
            auto pos = line.find('=');
            if (pos == std::string::npos)
            {
                continue;
            }

            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);

            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            if (currentSection.empty())
            {
                globalValues[key] = value;
            }
            else
            {
                sections[currentSection][key] = value;
            }
        }

        // Apply global values
        if (globalValues.count("logsize"))
        {
            config.logsize = globalValues["logsize"];
        }
        if (globalValues.count("logfile"))
        {
            config.logfile = globalValues["logfile"];
        }

        // Check if we have sections or simple config
        if (sections.empty())
        {
            // Simple single-device configuration
            std::string typeStr = globalValues.count("device-type")
                                      ? globalValues["device-type"]
                                      : "uart";

            DeviceConfig device;
            device.base.name = "default";
            device.base.device = globalValues.count("device")
                                     ? globalValues["device"]
                                     : config.defaultDevice;
            device.base.socketPath = globalValues.count("socket-path")
                                         ? globalValues["socket-path"]
                                         : config.defaultSocketPath;
            device.base.baudRate =
                globalValues.count("local-tty-baud")
                    ? std::stoi(globalValues["local-tty-baud"])
                    : config.defaultBaudRate;

            // Parse socket-mode (octal string like "0666")
            if (globalValues.count("socket-mode"))
            {
                device.base.socketMode =
                    std::stoi(globalValues["socket-mode"], nullptr, 8);
            }

            // Parse socket-group
            if (globalValues.count("socket-group"))
            {
                device.base.socketGroup = globalValues["socket-group"];
            }

            device.specific = createSpecificConfig(typeStr, globalValues);
            device.mux = createMuxConfig(globalValues);

            config.devices.push_back(device);
        }
        else
        {
            // Multi-device configuration
            for (const auto& [sectionName, values] : sections)
            {
                // Only process console.* sections
                if (sectionName.find("console.") != 0)
                {
                    continue;
                }

                std::string typeStr = values.count("device-type")
                                          ? values.at("device-type")
                                          : "uart";

                DeviceConfig device;
                device.base.name =
                    sectionName.substr(8); // Remove "console." prefix

                if (values.count("device"))
                {
                    device.base.device = values.at("device");
                }

                if (values.count("socket-path"))
                {
                    device.base.socketPath = values.at("socket-path");
                }
                else
                {
                    // Generate default socket path
                    device.base.socketPath =
                        "/tmp/obmc-console-" + device.base.name + ".sock";
                }

                if (values.count("local-tty-baud"))
                {
                    device.base.baudRate =
                        std::stoi(values.at("local-tty-baud"));
                }

                // Parse socket-mode (octal string like "0666")
                if (values.count("socket-mode"))
                {
                    device.base.socketMode =
                        std::stoi(values.at("socket-mode"), nullptr, 8);
                }

                // Parse socket-group
                if (values.count("socket-group"))
                {
                    device.base.socketGroup = values.at("socket-group");
                }

                device.specific = createSpecificConfig(typeStr, values);
                device.mux = createMuxConfig(values);

                config.devices.push_back(device);
            }
        }

        return config;
    }
};

} // namespace NSNAME
