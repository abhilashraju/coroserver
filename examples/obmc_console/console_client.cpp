/**
 * @file console_client.cpp
 * @brief OBMC Console Client - Connects to console server via Unix socket
 *
 * This client implements functionality similar to obmc-console-client:
 * - Connects to Unix domain socket
 * - Forwards stdin to the socket (user input to console)
 * - Forwards socket data to stdout (console output to user)
 * - Handles escape sequences for disconnection
 * - Sets terminal to raw mode for proper console interaction
 *
 * Usage:
 *   console_client --socket /tmp/obmc-console.sock
 *   console_client -s /tmp/obmc-console.sock
 *
 * Escape sequence: Enter ~ . (newline, tilde, dot) to disconnect
 */

#include "beastdefs.hpp"
#include "command_line_parser.hpp"
#include "logger.hpp"
#include "unix_client.hpp"

#include <termios.h>
#include <unistd.h>

#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/ssl.hpp>

#include <csignal>
#include <iostream>

using namespace NSNAME;
using unix_socket = boost::asio::local::stream_protocol;

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
 * @brief Terminal manager for raw mode
 */
class TerminalManager
{
  public:
    TerminalManager()
    {
        // Save original terminal settings
        if (tcgetattr(STDIN_FILENO, &origTermios_) == 0)
        {
            savedTermios_ = true;
        }
    }

    ~TerminalManager()
    {
        restore();
    }

    bool setRawMode()
    {
        if (!savedTermios_)
        {
            return false;
        }

        struct termios raw = origTermios_;

        // Set raw mode
        cfmakeraw(&raw);

        // Apply settings
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
        {
            LOG_ERROR("Failed to set terminal to raw mode");
            return false;
        }

        LOG_INFO("Terminal set to raw mode");
        return true;
    }

    void restore()
    {
        if (savedTermios_)
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &origTermios_);
            LOG_INFO("Terminal restored to original mode");
        }
    }

  private:
    struct termios origTermios_;
    bool savedTermios_ = false;
};

/**
 * @brief Escape sequence detector (SSH-style: Enter ~ .)
 */
class EscapeDetector
{
  public:
    bool process(char c)
    {
        switch (state_)
        {
            case State::Normal:
                if (c == '\r' || c == '\n')
                {
                    state_ = State::AfterNewline;
                }
                break;

            case State::AfterNewline:
                if (c == '~')
                {
                    state_ = State::AfterTilde;
                    return false; // Don't send tilde yet
                }
                else
                {
                    state_ = State::Normal;
                }
                break;

            case State::AfterTilde:
                if (c == '.')
                {
                    LOG_INFO("Escape sequence detected, disconnecting...");
                    return true; // Escape detected!
                }
                else
                {
                    // False alarm, send the tilde we held back
                    state_ = State::Normal;
                }
                break;
        }
        return false;
    }

  private:
    enum class State
    {
        Normal,
        AfterNewline,
        AfterTilde
    };
    State state_ = State::Normal;
};

/**
 * @brief Console client
 */
class ConsoleClient
{
  public:
    ConsoleClient(net::any_io_executor io_context,
                  const std::string& socketPath,
                  boost::asio::ssl::context& sslContext) :
        io_context_(io_context), socketPath_(socketPath),
        client_(io_context, sslContext),
        stdinStream_(io_context, ::dup(STDIN_FILENO))
    {}

    net::awaitable<void> run()
    {
        try
        {
            // Connect to Unix socket
            LOG_INFO("Connecting to console server: {}", socketPath_);
            auto ec = co_await client_.connect(socketPath_);

            if (ec)
            {
                LOG_ERROR("Failed to connect to socket: {}", ec.message());
                co_return;
            }

            LOG_INFO("Connected to console server");
            LOG_INFO("Press Enter ~ . to disconnect");

            // Set terminal to raw mode
            if (!termManager_.setRawMode())
            {
                LOG_ERROR("Failed to set terminal to raw mode");
                co_return;
            }

            // Start bidirectional forwarding
            boost::asio::co_spawn(
                io_context_,
                [this]() -> net::awaitable<void> { co_await readFromSocket(); },
                boost::asio::detached);

            co_await readFromStdin();
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception in console client: {}", e.what());
        }

        termManager_.restore();
    }

  private:
    /**
     * @brief Read from socket and write to stdout
     */
    net::awaitable<void> readFromSocket()
    {
        try
        {
            std::array<char, 1024> buffer;

            while (!shutdownRequested)
            {
                auto [ec, bytesRead] =
                    co_await client_.read(boost::asio::buffer(buffer));

                if (ec)
                {
                    if (ec == boost::asio::error::eof)
                    {
                        LOG_INFO("Server closed connection");
                    }
                    else if (ec != boost::asio::error::operation_aborted)
                    {
                        LOG_ERROR("Socket read error: {}", ec.message());
                    }
                    break;
                }

                if (bytesRead > 0)
                {
                    // Write to stdout
                    ssize_t written =
                        ::write(STDOUT_FILENO, buffer.data(), bytesRead);
                    if (written < 0)
                    {
                        LOG_ERROR("Failed to write to stdout");
                        break;
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception reading from socket: {}", e.what());
        }

        shutdownRequested = true;
    }

    /**
     * @brief Read from stdin and write to socket
     */
    net::awaitable<void> readFromStdin()
    {
        try
        {
            std::array<char, 1024> buffer;
            EscapeDetector escapeDetector;

            while (!shutdownRequested)
            {
                boost::system::error_code ec;
                size_t bytesRead = co_await stdinStream_.async_read_some(
                    boost::asio::buffer(buffer),
                    boost::asio::redirect_error(boost::asio::use_awaitable,
                                                ec));

                if (ec)
                {
                    if (ec != boost::asio::error::operation_aborted)
                    {
                        LOG_ERROR("Stdin read error: {}", ec.message());
                    }
                    break;
                }

                if (bytesRead > 0)
                {
                    // Check for escape sequence
                    bool escapeDetected = false;
                    for (size_t i = 0; i < bytesRead; ++i)
                    {
                        if (escapeDetector.process(buffer[i]))
                        {
                            escapeDetected = true;
                            break;
                        }
                    }

                    if (escapeDetected)
                    {
                        LOG_INFO("Disconnecting...");
                        break;
                    }

                    // Write to socket
                    auto [ec, bytesWritten] = co_await client_.write(
                        boost::asio::buffer(buffer.data(), bytesRead));

                    if (ec)
                    {
                        LOG_ERROR("Socket write error: {}", ec.message());
                        break;
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception reading from stdin: {}", e.what());
        }

        shutdownRequested = true;
    }

    net::any_io_executor io_context_;
    std::string socketPath_;
    UnixClient client_;
    boost::asio::posix::stream_descriptor stdinStream_;
    TerminalManager termManager_;
};

int main(int argc, const char* argv[])
{
    try
    {
        // Parse command line arguments
        auto args = parseCommandline(argc, argv);
        auto [socketOpt] = getArgs(args, "--socket,-s");

        // Setup signal handlers
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        std::string socketPath =
            std::string(socketOpt.value_or("/tmp/obmc-console.sock"));

        LOG_INFO("Starting console client");
        LOG_INFO("  Socket: {}", socketPath);

        // Create IO context
        net::io_context io_context;

        // Create SSL context (for potential SSL/TLS support)
        boost::asio::ssl::context sslContext(
            boost::asio::ssl::context::sslv23_client);

        // Setup signal handling
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait(
            [&io_context](const boost::system::error_code& ec, int signal) {
                if (!ec)
                {
                    LOG_INFO("Received signal {}, stopping client...", signal);
                    shutdownRequested = true;
                    io_context.stop();
                }
            });

        // Create and run console client
        ConsoleClient client(io_context.get_executor(), socketPath, sslContext);

        boost::asio::co_spawn(io_context, client.run(), boost::asio::detached);

        // Run the IO context
        io_context.run();

        LOG_INFO("Console client stopped");

        return EXIT_SUCCESS;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return EXIT_FAILURE;
    }
}
