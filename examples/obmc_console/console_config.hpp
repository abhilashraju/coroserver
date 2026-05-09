#pragma once

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

namespace NSNAME
{

/**
 * @brief Configuration for console server
 */
struct ConsoleConfig
{
    std::string lpcAddress = "0x3f8";
    int sirq = 4;
    int localTtyBaud = 115200;
    std::string logsize = "256k";
    std::string logfile = "/var/log/obmc-console.log";
    std::string device = "/dev/ttyS0";
    std::string socketPath = "/tmp/obmc-console.sock";

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
     * @brief Load configuration from file
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
        std::unordered_map<std::string, std::string> values;

        while (std::getline(file, line))
        {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#')
            {
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

            values[key] = value;
        }

        // Apply values
        if (values.count("lpc-address"))
        {
            config.lpcAddress = values["lpc-address"];
        }
        if (values.count("sirq"))
        {
            config.sirq = std::stoi(values["sirq"]);
        }
        if (values.count("local-tty-baud"))
        {
            config.localTtyBaud = std::stoi(values["local-tty-baud"]);
        }
        if (values.count("logsize"))
        {
            config.logsize = values["logsize"];
        }
        if (values.count("logfile"))
        {
            config.logfile = values["logfile"];
        }
        if (values.count("device"))
        {
            config.device = values["device"];
        }
        if (values.count("socket-path"))
        {
            config.socketPath = values["socket-path"];
        }

        return config;
    }
};

} // namespace NSNAME
