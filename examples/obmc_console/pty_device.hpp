#pragma once
#include "beastdefs.hpp"
#include "file_descriptor.hpp"
#include "logger.hpp"

#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <boost/asio/posix/stream_descriptor.hpp>

#include <memory>
#include <string>

namespace NSNAME
{

/**
 * @brief PTY configuration structure
 */
struct PtyConfig
{
    std::string shellPath = "/bin/sh";      // Shell to execute
    std::vector<std::string> shellArgs;     // Shell arguments (e.g., "-i")
    std::map<std::string, std::string> env; // Environment variables
    bool spawnShell = true;                 // Whether to spawn a shell process

    PtyConfig() : shellArgs{"-i"}
    {
        // Default environment
        env["TERM"] = "xterm";
        env["PS1"] = "$ ";
    }
};

/**
 * @brief PTY device wrapper with async I/O support
 */
class PtyDevice
{
  public:
    PtyDevice(net::any_io_executor io_context, const PtyConfig& config) :
        config_(config), stream_(io_context), master_fd_(), slave_fd_(),
        shell_pid_(-1)
    {}

    ~PtyDevice()
    {
        close();
    }

    /**
     * @brief Open and configure the PTY device
     * @return error_code indicating success or failure
     */
    boost::system::error_code open()
    {
        char slave_name[256];

        // Create pseudo-terminal pair
        int master_tmp, slave_tmp;
        if (openpty(&master_tmp, &slave_tmp, slave_name, nullptr, nullptr) < 0)
        {
            LOG_ERROR("Failed to create PTY pair");
            return boost::system::error_code(errno,
                                             boost::system::system_category());
        }

        master_fd_.reset(master_tmp);
        slave_fd_.reset(slave_tmp);

        slave_name_ = slave_name;
        LOG_INFO("PTY pair created: master_fd={}, slave={}", master_fd_.get(),
                 slave_name_);

        // Spawn shell if configured
        if (config_.spawnShell)
        {
            auto ec = spawnShell();
            if (ec)
            {
                return ec;
            }
        }

        // Assign the master file descriptor to the stream
        stream_.assign(master_fd_.get());

        LOG_INFO("PTY device opened successfully");
        return boost::system::error_code{};
    }

    /**
     * @brief Close the PTY device
     */
    void close()
    {
        if (master_fd_.isValid())
        {
            stream_.release();

            // Terminate shell process if running
            if (shell_pid_ > 0)
            {
                LOG_INFO("Terminating shell process (PID: {})", shell_pid_);
                kill(shell_pid_, SIGTERM);

                // Wait for process to exit (with timeout)
                int status;
                for (int i = 0; i < 10; i++)
                {
                    if (waitpid(shell_pid_, &status, WNOHANG) > 0)
                    {
                        break;
                    }
                    usleep(100000); // 100ms
                }

                // Force kill if still running
                if (waitpid(shell_pid_, &status, WNOHANG) == 0)
                {
                    LOG_WARNING("Shell process did not exit, force killing");
                    kill(shell_pid_, SIGKILL);
                    waitpid(shell_pid_, &status, 0);
                }

                shell_pid_ = -1;
            }

            // RAII wrappers automatically clean up file descriptors
            master_fd_.reset();
            slave_fd_.reset();

            LOG_INFO("PTY device closed");
        }
    }

    /**
     * @brief Async read from PTY
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
            LOG_ERROR("Error reading from PTY: {}", ec.message());
        }

        co_return std::make_pair(ec, bytes_transferred);
    }

    /**
     * @brief Async write to PTY
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
            LOG_ERROR("Error writing to PTY: {}", ec.message());
        }

        co_return std::make_pair(ec, bytes_transferred);
    }

    /**
     * @brief Check if PTY device is open
     */
    bool isOpen() const
    {
        return master_fd_.isValid() && stream_.is_open();
    }

    /**
     * @brief Get the slave PTY name
     */
    const std::string& getSlaveName() const
    {
        return slave_name_;
    }

    /**
     * @brief Get the shell process ID
     */
    pid_t getShellPid() const
    {
        return shell_pid_;
    }

    /**
     * @brief Get the underlying stream descriptor
     */
    net::posix::stream_descriptor& getStream()
    {
        return stream_;
    }

  private:
    /**
     * @brief Spawn shell process on slave PTY
     */
    boost::system::error_code spawnShell()
    {
        shell_pid_ = fork();

        if (shell_pid_ < 0)
        {
            LOG_ERROR("Failed to fork shell process");
            return boost::system::error_code(errno,
                                             boost::system::system_category());
        }

        if (shell_pid_ == 0)
        {
            // Child process
            master_fd_.reset(); // Child doesn't need master

            // Create new session and make slave the controlling terminal
            if (setsid() < 0)
            {
                perror("setsid");
                exit(1);
            }

            if (ioctl(slave_fd_.get(), TIOCSCTTY, 0) < 0)
            {
                perror("ioctl TIOCSCTTY");
                exit(1);
            }

            // Redirect stdin/stdout/stderr to slave
            if (dup2(slave_fd_.get(), STDIN_FILENO) < 0 ||
                dup2(slave_fd_.get(), STDOUT_FILENO) < 0 ||
                dup2(slave_fd_.get(), STDERR_FILENO) < 0)
            {
                perror("dup2");
                exit(1);
            }

            slave_fd_.reset(); // Close slave after duplication

            // Set environment variables
            for (const auto& [key, value] : config_.env)
            {
                setenv(key.c_str(), value.c_str(), 1);
            }

            // Prepare arguments for execvp
            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(config_.shellPath.c_str()));
            for (const auto& arg : config_.shellArgs)
            {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);

            // Execute shell
            execvp(config_.shellPath.c_str(), argv.data());

            // If execvp returns, it failed
            perror("execvp");
            exit(1);
        }

        // Parent process
        slave_fd_.reset(); // Parent doesn't need slave

        LOG_INFO("Shell spawned: {} (PID: {})", config_.shellPath, shell_pid_);

        return boost::system::error_code{};
    }

    PtyConfig config_;
    net::posix::stream_descriptor stream_;
    FileDescriptor master_fd_;
    FileDescriptor slave_fd_;
    pid_t shell_pid_;
    std::string slave_name_;
};

} // namespace NSNAME
