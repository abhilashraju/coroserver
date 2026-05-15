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

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/ssl.hpp>

#include <csignal>
#include <iostream>
#include <stop_token>

using namespace NSNAME;
using unix_socket = boost::asio::local::stream_protocol;

// Global stop source for cooperative cancellation
std::stop_source globalStopSource;

void signalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        LOG_INFO("Shutdown signal received");
        globalStopSource.request_stop();
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

        // Use canonical mode with echo (line-buffered input)
        // This allows users to see what they type and edit with backspace
        raw.c_lflag |= (ECHO | ECHOE | ECHOK); // Enable echo
        raw.c_lflag |= ICANON; // Enable canonical mode (line buffering)

        // Disable special signal characters (Ctrl+C, Ctrl+Z, etc.)
        raw.c_lflag &= ~ISIG;

        // Apply settings
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
        {
            LOG_ERROR("Failed to set terminal mode");
            return false;
        }

        LOG_INFO("Terminal set to canonical mode with echo");
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
 * @brief Input processor - accumulates characters until newline
 * Also detects escape sequence (Enter ~ .)
 */
class InputProcessor
{
  public:
    enum class Result
    {
        Continue,   // Keep accumulating
        SendBuffer, // Send accumulated buffer
        Escape      // Escape sequence detected
    };

    Result process(char c)
    {
        switch (state_)
        {
            case State::Normal:
                buffer_.push_back(c);
                if (c == '\r' || c == '\n')
                {
                    // Send line immediately, then check for escape on next
                    // input
                    state_ = State::AfterNewline;
                    return Result::SendBuffer;
                }
                return Result::Continue;

            case State::AfterNewline:
                if (c == '~')
                {
                    state_ = State::AfterTilde;
                    return Result::Continue; // Don't add tilde yet, might be
                                             // escape
                }
                else
                {
                    // Not an escape sequence, normal character
                    state_ = State::Normal;
                    buffer_.push_back(c);
                    return Result::Continue;
                }

            case State::AfterTilde:
                if (c == '.')
                {
                    LOG_INFO("Escape sequence detected, disconnecting...");
                    return Result::Escape;
                }
                else
                {
                    // False alarm, add the tilde and current char
                    buffer_.push_back('~');
                    buffer_.push_back(c);
                    state_ = State::Normal;
                    return Result::Continue;
                }
        }
        return Result::Continue;
    }

    const std::vector<char>& getBuffer() const
    {
        return buffer_;
    }

    void clearBuffer()
    {
        buffer_.clear();
    }

  private:
    enum class State
    {
        Normal,
        AfterNewline,
        AfterTilde
    };
    State state_ = State::Normal;
    std::vector<char> buffer_;
};

/**
 * @brief Console client
 */
class ConsoleClient
{
  public:
    ConsoleClient(net::any_io_executor io_context,
                  const std::string& socketPath, std::stop_token stopToken) :
        io_context_(io_context), socketPath_(socketPath), stopToken_(stopToken),
        client_(io_context), stdinStream_(io_context)
    {
        // Use STDIN_FILENO directly (don't duplicate)
        // Set stdin to non-blocking mode
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (flags >= 0)
        {
            fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        }

        // Assign STDIN_FILENO directly to the stream
        stdinStream_.assign(STDIN_FILENO);
    }

    ~ConsoleClient()
    {
        cleanup();
    }

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
                globalStopSource.request_stop();
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

        cleanup();
    }

    void cleanup()
    {
        // Release stdin stream without closing the fd
        if (stdinStream_.is_open())
        {
            stdinStream_.release(); // Release ownership without closing
        }

        // Restore terminal settings (including blocking mode)
        termManager_.restore();

        // Restore stdin to blocking mode
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (flags >= 0)
        {
            fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
        }

        // Close client connection
        client_.close();
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

            while (!stopToken_.stop_requested())
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
                        continue;
                    }
                    globalStopSource.request_stop();
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
            globalStopSource.request_stop();
        }

        globalStopSource.request_stop();
        // Cancel stdin operations to exit immediately
        stdinStream_.cancel();
    }

    /**
     * @brief Read from stdin and write to socket
     */
    net::awaitable<void> readFromStdin()
    {
        try
        {
            std::array<char, 1024> buffer;
            InputProcessor inputProcessor;

            while (!stopToken_.stop_requested())
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
                    // Process each character through input processor
                    for (size_t i = 0; i < bytesRead; ++i)
                    {
                        auto result = inputProcessor.process(buffer[i]);

                        if (result == InputProcessor::Result::Escape)
                        {
                            LOG_INFO("Disconnecting...");
                            globalStopSource.request_stop();
                            break;
                        }
                        else if (result == InputProcessor::Result::SendBuffer)
                        {
                            // Send the accumulated buffer directly
                            const auto& sendBuffer = inputProcessor.getBuffer();
                            if (!sendBuffer.empty())
                            {
                                LOG_DEBUG("Sending {} bytes to server",
                                          sendBuffer.size());

                                auto [ec, bytesWritten] =
                                    co_await client_.write(boost::asio::buffer(
                                        sendBuffer.data(), sendBuffer.size()));

                                if (ec)
                                {
                                    LOG_ERROR("Socket write error: {}",
                                              ec.message());
                                    globalStopSource.request_stop();
                                    break;
                                }

                                LOG_DEBUG("Successfully sent {} bytes",
                                          bytesWritten);
                            }
                            inputProcessor.clearBuffer();
                        }
                        // Result::Continue means keep accumulating
                    }

                    if (stopToken_.stop_requested())
                    {
                        break;
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception reading from stdin: {}", e.what());
            globalStopSource.request_stop();
        }

        globalStopSource.request_stop();
        // Close socket to exit readFromSocket() immediately
        client_.close();
    }

    net::any_io_executor io_context_;
    std::string socketPath_;
    std::stop_token stopToken_;
    UnixClientPlain client_;
    boost::asio::posix::stream_descriptor stdinStream_;
    TerminalManager termManager_;
};

int main(int argc, const char* argv[])
{
    try
    {
        reactor::getLogger().setLogLevel(reactor::LogLevel::DEBUG);
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

        // Setup signal handling
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait(
            [&io_context](const boost::system::error_code& ec, int signal) {
                if (!ec)
                {
                    LOG_INFO("Received signal {}, stopping client...", signal);
                    globalStopSource.request_stop();
                    io_context.stop();
                }
            });

        // Create and run console client
        ConsoleClient client(io_context.get_executor(), socketPath,
                             globalStopSource.get_token());

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
