#pragma once
#include "logger.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <boost/asio.hpp>

#include <array>
#include <string>

namespace net = boost::asio;

// SPDM stdout/stderr redirection to logger using boost asio stream_descriptor
namespace spdm_io_redirect
{

/**
 * @brief Redirects stdout/stderr from libspdm library to the logger framework
 *
 * Uses boost::asio::posix::stream_descriptor and coroutines to asynchronously
 * read output from redirected stdout/stderr and log it through the existing
 * logger infrastructure.
 */
class StdoutRedirector
{
  private:
    net::io_context& io_context;
    int saved_stdout{-1};
    int saved_stderr{-1};
    int pipe_fd[2]{-1, -1};
    std::unique_ptr<net::posix::stream_descriptor> stream_desc;
    bool running{false};

    /**
     * @brief Coroutine that reads from the stream descriptor and logs output
     */
    net::awaitable<void> readLoop()
    {
        std::vector<char> buffer(4096);
        boost::system::error_code ec{};

        while (running && !ec)
        {
            auto size = co_await stream_desc->async_read_some(
                net::buffer(buffer),
                net::redirect_error(net::use_awaitable, ec));

            if (ec && ec != net::error::eof)
            {
                LOG_ERROR("[SPDM-LIB] Pipe read error: {}", ec.message());
                break;
            }

            if (size > 0)
            {
                std::string message(buffer.data(), size);

                // Remove trailing newline if present
                while (!message.empty() &&
                       (message.back() == '\n' || message.back() == '\r'))
                {
                    message.pop_back();
                }

                // Log the captured output
                if (!message.empty())
                {
                    LOG_INFO("[SPDM-LIB] {}", message);
                }
            }

            if (ec == net::error::eof)
            {
                break;
            }
        }

        co_return;
    }

  public:
    StdoutRedirector(net::io_context& io) : io_context(io)
    {
        // Create pipe for redirection
        if (pipe(pipe_fd) == -1)
        {
            LOG_ERROR("Failed to create pipe for stdout redirection");
            return;
        }

        // Save original stdout and stderr
        saved_stdout = dup(STDOUT_FILENO);
        saved_stderr = dup(STDERR_FILENO);

        // Redirect stdout and stderr to pipe write end
        if (dup2(pipe_fd[1], STDOUT_FILENO) == -1 ||
            dup2(pipe_fd[1], STDERR_FILENO) == -1)
        {
            LOG_ERROR("Failed to redirect stdout/stderr");
            close(pipe_fd[0]);
            close(pipe_fd[1]);
            return;
        }

        // Close write end in parent process (child processes will inherit it)
        close(pipe_fd[1]);
        pipe_fd[1] = -1;

        // Create stream_descriptor from the read end of the pipe
        stream_desc = std::make_unique<net::posix::stream_descriptor>(
            io_context, pipe_fd[0]);
        pipe_fd[0] = -1; // stream_descriptor now owns this fd

        running = true;

        // Start the coroutine to read from pipe
        net::co_spawn(
            io_context,
            [this]() -> net::awaitable<void> { co_await readLoop(); },
            net::detached);

        LOG_INFO("SPDM stdout/stderr redirection enabled");
    }

    ~StdoutRedirector()
    {
        running = false;

        // Restore original stdout and stderr
        if (saved_stdout != -1)
        {
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
        }
        if (saved_stderr != -1)
        {
            dup2(saved_stderr, STDERR_FILENO);
            close(saved_stderr);
        }

        // Close stream descriptor (this will cause readLoop to exit)
        if (stream_desc && stream_desc->is_open())
        {
            stream_desc->close();
            stream_desc.reset();
        }

        // Close pipe read end if still open
        if (pipe_fd[0] != -1)
        {
            close(pipe_fd[0]);
        }
        if (pipe_fd[1] != -1)
        {
            close(pipe_fd[1]);
        }
    }

    // Delete copy and move constructors
    StdoutRedirector(const StdoutRedirector&) = delete;
    StdoutRedirector& operator=(const StdoutRedirector&) = delete;
    StdoutRedirector(StdoutRedirector&&) = delete;
    StdoutRedirector& operator=(StdoutRedirector&&) = delete;
};

// Global redirector instance
inline StdoutRedirector*& getRedirector()
{
    static StdoutRedirector* redirector = nullptr;
    return redirector;
}

inline void enableSpdmLogging(net::io_context& io_context)
{
    if (getRedirector() == nullptr)
    {
        getRedirector() = new StdoutRedirector(io_context);
    }
}

inline void disableSpdmLogging()
{
    if (getRedirector() != nullptr)
    {
        delete getRedirector();
        getRedirector() = nullptr;
        LOG_INFO("SPDM stdout/stderr redirection disabled");
    }
}

} // namespace spdm_io_redirect
