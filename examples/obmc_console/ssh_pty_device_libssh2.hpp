#pragma once
#include "async_wait.hpp"
#include "beastdefs.hpp"
#include "logger.hpp"
#include "ssh_client.hpp"

#include <boost/asio/steady_timer.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace NSNAME
{

/**
 * @brief SSH PTY configuration structure for remote connections
 */
struct SshPtyConfig
{
    std::string remoteHost;                 // Remote host IP/hostname
    std::string remoteUser;                 // SSH username
    std::string remotePassword;             // SSH password (optional)
    std::string sshKeyPath;                 // SSH private key path (optional)
    std::string sshKeyPassphrase;           // Key passphrase (optional)
    int remotePort = 22;                    // SSH port
    std::string remoteShell = "/bin/bash";  // Shell to run on remote
    std::map<std::string, std::string> env; // Environment variables
    int connectTimeout = 30;                // Connection timeout in seconds
    bool verifyHostKey = false;             // Verify host key (default: no)

    SshPtyConfig()
    {
        // Default environment
        env["TERM"] = "xterm-256color";
    }

    // Convert to SshConfig for SSHClient
    SshConfig toSshConfig() const
    {
        SshConfig config;
        config.remoteHost = remoteHost;
        config.remoteUser = remoteUser;
        config.remotePassword = remotePassword;
        config.sshKeyPath = sshKeyPath;
        config.sshKeyPassphrase = sshKeyPassphrase;
        config.remotePort = remotePort;
        config.connectTimeout = connectTimeout;
        config.verifyHostKey = verifyHostKey;
        return config;
    }
};

/**
 * @brief SSH PTY device using libssh2 - connects to remote machine via SSH
 *
 * This device uses libssh2 to establish SSH connections programmatically,
 * supporting both password and key-based authentication. It reads data
 * directly from the SSH channel and uses a callback mechanism to deliver
 * data to the server, which then broadcasts to multiple clients.
 *
 * Architecture (NO PTY master/slave):
 *   Console Clients <-> Unix Socket <-> Console Server <-> SshPtyDevice
 *                                            |                    |
 *                                       Ring Buffer          SSH Channel
 *                                            |                    |
 *                                       Broadcast            Remote Shell
 *
 * This eliminates the race condition where multiple clients compete for
 * the same PTY master stream.
 */
class SshPtyDevice : public std::enable_shared_from_this<SshPtyDevice>
{
  public:
    SshPtyDevice(net::any_io_executor io_context, const SshPtyConfig& config) :
        config(config), executor(io_context),
        sshClient(
            std::make_shared<SSHClient>(io_context, config.toSshConfig())),
        channel()
    {}

    ~SshPtyDevice()
    {
        close();
    }

    /**
     * @brief Open and configure the SSH PTY device (async version)
     * @return awaitable error_code indicating success or failure
     */
    net::awaitable<boost::system::error_code> openAsync()
    {
        // Connect and authenticate using SSHClient
        auto ec = co_await sshClient->connect();
        if (ec)
        {
            co_return ec;
        }

        // Open shell channel (async)
        ec = co_await openShellChannelAsync();
        if (ec)
        {
            co_return ec;
        }

        LOG_INFO("SSH PTY device opened successfully, connected to {}@{}:{}",
                 config.remoteUser, config.remoteHost, config.remotePort);
        co_return boost::system::error_code{};
    }

    /**
     * @brief Open and configure the SSH PTY device (synchronous version)
     * @return error_code indicating success or failure
     */
    boost::system::error_code open()
    {
        // This is a synchronous wrapper that blocks until connection completes
        // For use in non-coroutine contexts
        boost::system::error_code result;

        net::co_spawn(
            executor,
            [this]() -> net::awaitable<boost::system::error_code> {
                co_return co_await openAsync();
            }(),
            [&result](std::exception_ptr e, boost::system::error_code ec) {
                if (e)
                {
                    try
                    {
                        std::rethrow_exception(e);
                    }
                    catch (const std::exception& ex)
                    {
                        LOG_ERROR("Exception during SSH open: {}", ex.what());
                        result = boost::system::error_code(
                            ECONNREFUSED, boost::system::system_category());
                    }
                }
                else
                {
                    result = ec;
                }
            });

        // Run the io_context until the operation completes
        // Note: This assumes the executor is not already running
        return result;
    }

    /**
     * @brief Close the SSH PTY device
     */
    void close()
    {
        // RAII wrapper automatically cleans up channel
        channel.reset();

        // Close SSH client
        if (sshClient)
        {
            sshClient->close();
        }

        LOG_INFO("SSH PTY device closed");
    }

    /**
     * @brief Async read from SSH channel (for server's deviceReadLoop)
     * @param buffer Buffer to read into
     * @return Pair of error_code and bytes read
     */
    net::awaitable<std::pair<boost::system::error_code, std::size_t>> read(
        net::mutable_buffer buffer)
    {
        co_return co_await sshClient->read(channel.get(), buffer);
    }

    /**
     * @brief Async write to SSH channel
     * @param buffer Buffer to write from
     * @return Pair of error_code and bytes written
     */
    net::awaitable<std::pair<boost::system::error_code, std::size_t>> write(
        net::const_buffer buffer)
    {
        co_return co_await sshClient->write(channel.get(), buffer);
    }

    /**
     * @brief Check if device is open
     */
    bool isOpen() const
    {
        return sshClient && sshClient->isOpen();
    }

    /**
     * @brief Get slave PTY name (returns empty for SSH - no PTY)
     */
    std::string getSlaveName() const
    {
        return ""; // No PTY slave in this implementation
    }

    /**
     * @brief Get connection info string
     */
    std::string getConnectionInfo() const
    {
        return sshClient ? sshClient->getConnectionInfo() : "";
    }

    /**
     * @brief Get SSH process PID (returns 0 for libssh2 - no separate process)
     */
    pid_t getSshPid() const
    {
        return 0; // libssh2 runs in-process
    }

  private:
    /**
     * @brief Open shell channel (async version with retry for non-blocking)
     */
    net::awaitable<boost::system::error_code> openShellChannelAsync()
    {
        // Open a channel (retry on EAGAIN)
        while (!channel)
        {
            channel.reset(libssh2_channel_open_session(sshClient->session()));
            if (!channel)
            {
                char* errmsg;
                int errcode;
                libssh2_session_last_error(sshClient->session(), &errmsg,
                                           nullptr, 0);

                // Check if it's EAGAIN (would block)
                errcode = libssh2_session_last_errno(sshClient->session());
                if (errcode == LIBSSH2_ERROR_EAGAIN)
                {
                    // Wait a bit and retry
                    using namespace std::chrono_literals;
                    co_await waitFor(executor, 10ms);
                    continue;
                }

                LOG_ERROR("Failed to open SSH channel: {}", errmsg);
                co_return boost::system::error_code(
                    ECONNREFUSED, boost::system::system_category());
            }
        }

        // Request a PTY (retry on EAGAIN)
        int rc;
        while (true)
        {
            rc = libssh2_channel_request_pty(channel.get(), "xterm");
            if (rc == 0)
            {
                break; // Success
            }
            else if (rc == LIBSSH2_ERROR_EAGAIN)
            {
                // Wait a bit and retry
                using namespace std::chrono_literals;
                co_await waitFor(executor, 10ms);
                continue;
            }
            else
            {
                char* errmsg;
                libssh2_session_last_error(sshClient->session(), &errmsg,
                                           nullptr, 0);
                LOG_ERROR("Failed to request PTY: {} ({})", errmsg, rc);
                co_return boost::system::error_code(
                    ECONNREFUSED, boost::system::system_category());
            }
        }

        LOG_INFO("PTY requested successfully");

        // Set environment variables (before starting shell)
        for (const auto& [key, value] : config.env)
        {
            // Note: setenv may not work on all SSH servers
            while (true)
            {
                rc = libssh2_channel_setenv(channel.get(), key.c_str(),
                                            value.c_str());
                if (rc == 0 || rc == LIBSSH2_ERROR_REQUEST_DENIED)
                {
                    // Success or not supported - continue
                    break;
                }
                else if (rc == LIBSSH2_ERROR_EAGAIN)
                {
                    using namespace std::chrono_literals;
                    co_await waitFor(executor, 10ms);
                    continue;
                }
                else
                {
                    // Log warning but don't fail
                    LOG_WARNING("Failed to set env {}={}: {}", key, value, rc);
                    break;
                }
            }
        }

        // Give the PTY a moment to be ready
        using namespace std::chrono_literals;
        co_await waitFor(executor, 50ms);

        // Start shell (retry on EAGAIN)
        // Try using exec with shell path if shell() fails
        bool shellStarted = false;
        while (true)
        {
            rc = libssh2_channel_shell(channel.get());
            if (rc == 0)
            {
                shellStarted = true;
                break; // Success
            }
            else if (rc == LIBSSH2_ERROR_EAGAIN)
            {
                // Wait a bit and retry
                co_await waitFor(executor, 10ms);
                continue;
            }
            else if (rc == LIBSSH2_ERROR_CHANNEL_REQUEST_DENIED)
            {
                // Shell request denied, try exec instead
                LOG_WARNING(
                    "Shell request denied, trying exec with shell path: {}",
                    config.remoteShell);
                break;
            }
            else
            {
                char* errmsg;
                libssh2_session_last_error(sshClient->session(), &errmsg,
                                           nullptr, 0);
                LOG_ERROR("Failed to start shell: {} ({})", errmsg, rc);
                co_return boost::system::error_code(
                    ECONNREFUSED, boost::system::system_category());
            }
        }

        // If shell() failed, try exec
        if (!shellStarted)
        {
            while (true)
            {
                rc = libssh2_channel_exec(channel.get(),
                                          config.remoteShell.c_str());
                if (rc == 0)
                {
                    shellStarted = true;
                    break; // Success
                }
                else if (rc == LIBSSH2_ERROR_EAGAIN)
                {
                    co_await waitFor(executor, 10ms);
                    continue;
                }
                else
                {
                    char* errmsg;
                    libssh2_session_last_error(sshClient->session(), &errmsg,
                                               nullptr, 0);
                    LOG_ERROR("Failed to exec shell: {} ({})", errmsg, rc);
                    co_return boost::system::error_code(
                        ECONNREFUSED, boost::system::system_category());
                }
            }
        }

        if (!shellStarted)
        {
            LOG_ERROR("Failed to start shell or exec");
            co_return boost::system::error_code(
                ECONNREFUSED, boost::system::system_category());
        }

        LOG_INFO("SSH shell channel opened");
        co_return boost::system::error_code{};
    }

    SshPtyConfig config;
    net::any_io_executor executor;
    std::shared_ptr<SSHClient> sshClient;
    Ssh2ChannelPtr channel;
};

} // namespace NSNAME
