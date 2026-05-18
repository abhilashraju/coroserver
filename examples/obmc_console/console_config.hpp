#pragma once

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace NSNAME
{

/**
 * @brief Device type enumeration
 */
enum class DeviceType
{
    UART,
    VUART,
    PTY
};

/**
 * @brief Configuration for a single console device
 */
struct DeviceConfig
{
    std::string name;   // Console identifier (e.g., "host", "bmc")
    std::string device; // Device path (e.g., "/dev/ttyS0")
    DeviceType type = DeviceType::UART; // Device type
    int baudRate = 115200;              // Baud rate
    std::string socketPath;             // Unix socket path

    // VUART-specific settings
    std::string lpcAddress = "0x3f8";
    int sirq = 4;

    // PTY/Shell-specific settings
    std::string shellPath = "/bin/sh";  // Shell executable path
    std::vector<std::string> shellArgs; // Shell arguments

    // Multiplexing settings
    bool hasMux = false;
    int muxIndex = 0;
    std::vector<std::string> muxGpios; // GPIO names for multiplexing
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
            if (dev.name == name)
            {
                return dev;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Parse device type from string
     */
    static DeviceType parseDeviceType(const std::string& typeStr)
    {
        if (typeStr == "vuart" || typeStr == "VUART")
        {
            return DeviceType::VUART;
        }
        else if (typeStr == "pty" || typeStr == "PTY")
        {
            return DeviceType::PTY;
        }
        return DeviceType::UART;
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
     *   [console.bmc]
     *   device = /dev/ttyS4
     *   device-type = vuart
     *   lpc-address = 0x3f8
     *   sirq = 4
     *   socket-path = /tmp/obmc-console-bmc.sock
     *   local-tty-baud = 115200
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
            DeviceConfig device;
            device.name = "default";
            device.device = globalValues.count("device")
                                ? globalValues["device"]
                                : config.defaultDevice;
            device.socketPath = globalValues.count("socket-path")
                                    ? globalValues["socket-path"]
                                    : config.defaultSocketPath;
            device.baudRate = globalValues.count("local-tty-baud")
                                  ? std::stoi(globalValues["local-tty-baud"])
                                  : config.defaultBaudRate;

            if (globalValues.count("device-type"))
            {
                device.type = parseDeviceType(globalValues["device-type"]);
            }

            if (globalValues.count("lpc-address"))
            {
                device.lpcAddress = globalValues["lpc-address"];
            }
            if (globalValues.count("sirq"))
            {
                device.sirq = std::stoi(globalValues["sirq"]);
            }

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

                DeviceConfig device;
                device.name = sectionName.substr(8); // Remove "console." prefix

                if (values.count("device"))
                {
                    device.device = values.at("device");
                }
                if (values.count("socket-path"))
                {
                    device.socketPath = values.at("socket-path");
                }
                else
                {
                    // Generate default socket path
                    device.socketPath =
                        "/tmp/obmc-console-" + device.name + ".sock";
                }

                if (values.count("local-tty-baud"))
                {
                    device.baudRate = std::stoi(values.at("local-tty-baud"));
                }

                if (values.count("device-type"))
                {
                    device.type = parseDeviceType(values.at("device-type"));
                }

                if (values.count("lpc-address"))
                {
                    device.lpcAddress = values.at("lpc-address");
                }
                if (values.count("sirq"))
                {
                    device.sirq = std::stoi(values.at("sirq"));
                }

                // PTY/Shell settings
                if (values.count("shell-path"))
                {
                    device.shellPath = values.at("shell-path");
                }
                if (values.count("shell-args"))
                {
                    std::string args = values.at("shell-args");
                    std::istringstream iss(args);
                    std::string arg;
                    while (iss >> arg)
                    {
                        device.shellArgs.push_back(arg);
                    }
                }

                // Multiplexing support
                if (values.count("mux-index"))
                {
                    device.hasMux = true;
                    device.muxIndex = std::stoi(values.at("mux-index"));
                }
                if (values.count("mux-gpios"))
                {
                    device.hasMux = true;
                    std::string gpios = values.at("mux-gpios");
                    std::istringstream iss(gpios);
                    std::string gpio;
                    while (std::getline(iss, gpio, ','))
                    {
                        // Trim whitespace
                        gpio.erase(0, gpio.find_first_not_of(" \t"));
                        gpio.erase(gpio.find_last_not_of(" \t") + 1);
                        device.muxGpios.push_back(gpio);
                    }
                }

                config.devices.push_back(device);
            }
        }

        return config;
    }
};

} // namespace NSNAME
