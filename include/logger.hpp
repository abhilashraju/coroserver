#pragma once
#include "name_space.hpp"

#include <systemd/sd-journal.h>

#include <array>
#include <format>
#include <iostream>
#include <source_location>
#include <string>
#include <string_view>

#undef LOG_WARNING
#undef LOG_ERROR
#undef LOG_DEBUG
#undef LOG_INFO

namespace NSNAME
{

enum class LogLevel
{
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

// Returns a short human-readable prefix for each log level.
constexpr std::string_view levelPrefix(LogLevel level) noexcept
{
    switch (level)
    {
        case LogLevel::DEBUG:    return "Debug";
        case LogLevel::INFO:     return "Info";
        case LogLevel::WARNING:  return "Warning";
        case LogLevel::ERROR:    return "Error";
        case LogLevel::CRITICAL: return "Critical";
    }
    return "Unknown";
}

// Maps LogLevel to a systemd journal priority integer.
// Note: DEBUG is intentionally mapped to INFO (6) because OpenBMC sets
// MaxLevelSyslog/MaxLevelStore to Info, so lower values are never stored.
constexpr int toSystemdLevel(LogLevel level) noexcept
{
    switch (level)
    {
        case LogLevel::CRITICAL: return 2; // SD_CRIT
        case LogLevel::ERROR:    return 3; // SD_ERR
        case LogLevel::WARNING:  return 4; // SD_WARNING
        case LogLevel::INFO:     return 6; // SD_INFO
        case LogLevel::DEBUG:    return 6; // mapped to INFO — see note above
    }
    return 6; // fallback: treat unknown as debug/info
}

// Concept: a log backend must support streaming strings and flushing with a
// systemd priority level.
template <typename T>
concept LogBackend = requires(T& backend, std::string_view sv, int priority) {
    { backend << sv };
    { backend.flush(priority) };
};

template <LogBackend OutputStream>
class Logger
{
  public:
    Logger(LogLevel level, OutputStream& outputStream) :
        currentLogLevel(level), output(outputStream)
    {}

    void setLogLevel(LogLevel level) noexcept
    {
        currentLogLevel = level;
    }

    void log(const std::source_location& loc, LogLevel level,
             std::string_view message) const
    {
        if (!isEnabled(level))
        {
            return;
        }
        std::string_view fullPath = loc.file_name();
        std::string_view filename = fullPath.substr(fullPath.rfind('/') + 1);
        output << std::format("[{}] {}:{} {}", levelPrefix(level), filename,
                              loc.line(), message);
        output.flush(toSystemdLevel(level));
    }

  private:
    LogLevel currentLogLevel;
    OutputStream& output;

    bool isEnabled(LogLevel level) const noexcept
    {
        return level >= currentLogLevel;
    }
};

// Backend: systemd journal via sd_journal_send.
struct Lg2Logger
{
    Lg2Logger& operator<<(std::string_view data)
    {
        pending += data;
        return *this;
    }

    void flush(int priority)
    {
        sd_journal_send("MESSAGE=%s", pending.c_str(), "PRIORITY=%i", priority,
                        NULL);
        pending.clear();
    }

  private:
    std::string pending;
};

// Backend: std::ostream (stdout by default, useful for development/testing).
struct OStreamLogger
{
    explicit OStreamLogger(std::ostream& os) : stream(os) {}

    OStreamLogger& operator<<(std::string_view data)
    {
        stream << data;
        return *this;
    }

    void flush(int /*priority*/)
    {
        stream << '\n';
        stream.flush();
    }

  private:
    std::ostream& stream;
};

// Select the active backend at compile time.
// Define USE_LG2_LOGGER (e.g. via -DUSE_LG2_LOGGER) to route logs to the
// systemd journal.  Omit it (the default) to log to stdout — handy for
// development and unit tests.
#ifdef USE_LG2_LOGGER
inline Logger<Lg2Logger>& getLogger()
{
    static Lg2Logger backend;
    static Logger<Lg2Logger> logger(LogLevel::ERROR, backend);
    return logger;
}
#else
inline Logger<OStreamLogger>& getLogger()
{
    static OStreamLogger backend(std::cerr);
    static Logger<OStreamLogger> logger(LogLevel::DEBUG, backend);
    return logger;
}
#endif

} // namespace NSNAME

// Undefine any LOG_* macros that phosphor-logging or other headers may have
// injected after logger.hpp was first included.  These #undefs must sit
// immediately before our own definitions so that no later include can
// silently re-route log calls to a different backend.
#undef LOG_DEBUG
#undef LOG_INFO
#undef LOG_WARNING
#undef LOG_ERROR

// Logging macros.  The level prefix is derived from the LogLevel enum so it
// cannot drift out of sync with the enum definition.
#define LOG_DEBUG(message, ...)                                                \
    NSNAME::getLogger().log(std::source_location::current(),                   \
                            NSNAME::LogLevel::DEBUG,                           \
                            std::format(message __VA_OPT__(,) __VA_ARGS__))
#define LOG_INFO(message, ...)                                                 \
    NSNAME::getLogger().log(std::source_location::current(),                   \
                            NSNAME::LogLevel::INFO,                            \
                            std::format(message __VA_OPT__(,) __VA_ARGS__))
#define LOG_WARNING(message, ...)                                              \
    NSNAME::getLogger().log(std::source_location::current(),                   \
                            NSNAME::LogLevel::WARNING,                         \
                            std::format(message __VA_OPT__(,) __VA_ARGS__))
#define LOG_ERROR(message, ...)                                                \
    NSNAME::getLogger().log(std::source_location::current(),                   \
                            NSNAME::LogLevel::ERROR,                           \
                            std::format(message __VA_OPT__(,) __VA_ARGS__))

#define CLIENT_LOG_DEBUG(message, ...)   LOG_DEBUG(message, ##__VA_ARGS__)
#define CLIENT_LOG_INFO(message, ...)    LOG_INFO(message, ##__VA_ARGS__)
#define CLIENT_LOG_WARNING(message, ...) LOG_WARNING(message, ##__VA_ARGS__)
#define CLIENT_LOG_ERROR(message, ...)   LOG_ERROR(message, ##__VA_ARGS__)
