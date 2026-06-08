#pragma once
#include "async_wait.hpp"
#include "beastdefs.hpp"
#include "logger.hpp"

#include <arpa/inet.h>
#include <libssh2.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <boost/asio/steady_timer.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace NSNAME
{

/**
 * @brief Custom deleters for libssh2 resources (for use with std::unique_ptr)
 */
struct Ssh2ChannelDeleter
{
    void operator()(LIBSSH2_CHANNEL* channel) const
    {
        if (channel)
        {
            libssh2_channel_close(channel);
            libssh2_channel_free(channel);
        }
    }
};

struct Ssh2SessionDeleter
{
    void operator()(LIBSSH2_SESSION* session) const
    {
        if (session)
        {
            libssh2_session_disconnect(session, "Normal Shutdown");
            libssh2_session_free(session);
        }
    }
};

/**
 * @brief RAII wrapper types using std::unique_ptr for pointer-based resources
 */
using Ssh2ChannelPtr = std::unique_ptr<LIBSSH2_CHANNEL, Ssh2ChannelDeleter>;
using Ssh2SessionPtr = std::unique_ptr<LIBSSH2_SESSION, Ssh2SessionDeleter>;

/**
 * @brief RAII wrapper for libssh2 library initialization
 */
class Ssh2Library
{
  public:
    Ssh2Library() : initialized(false) {}

    ~Ssh2Library()
    {
        cleanup();
    }

    // Non-copyable
    Ssh2Library(const Ssh2Library&) = delete;
    Ssh2Library& operator=(const Ssh2Library&) = delete;

    // Movable
    Ssh2Library(Ssh2Library&& other) noexcept : initialized(other.initialized)
    {
        other.initialized = false;
    }

    Ssh2Library& operator=(Ssh2Library&& other) noexcept
    {
        if (this != &other)
        {
            cleanup();
            initialized = other.initialized;
            other.initialized = false;
        }
        return *this;
    }

    int init()
    {
        if (!initialized)
        {
            int rc = libssh2_init(0);
            if (rc == 0)
            {
                initialized = true;
            }
            return rc;
        }
        return 0;
    }

    bool isInitialized() const
    {
        return initialized;
    }

  private:
    void cleanup()
    {
        if (initialized)
        {
            libssh2_exit();
            initialized = false;
        }
    }

    bool initialized;
};

/**
 * @brief SSH connection configuration structure
 */
struct SshConfig
{
    std::string remoteHost;       // Remote host IP/hostname
    std::string remoteUser;       // SSH username
    std::string remotePassword;   // SSH password (optional)
    std::string sshKeyPath;       // SSH private key path (optional)
    std::string sshKeyPassphrase; // Key passphrase (optional)
    int remotePort = 22;          // SSH port
    int connectTimeout = 30;      // Connection timeout in seconds
    bool verifyHostKey = false;   // Verify host key (default: no)

    SshConfig() = default;
};

/**
 * @brief SSH Client - manages SSH connection and session
 *
 * This class provides a reusable SSH client similar to TcpClient,
 * handling connection, authentication, and session management.
 * It can be used by different components that need SSH connectivity.
 */
class SSHClient
{
  public:
    SSHClient(net::any_io_executor io_context, const SshConfig& config) :
        config(config), executor(io_context), sshSocket(io_context),
        sshSession(), ssh2Lib(), connected(false)
    {}

    ~SSHClient()
    {
        close();
    }

    /**
     * @brief Connect and authenticate to SSH server
     * @return error_code indicating success or failure
     */
    net::awaitable<boost::system::error_code> connect()
    {
        // Initialize libssh2
        int rc = ssh2Lib.init();
        if (rc != 0)
        {
            LOG_ERROR("libssh2 initialization failed: {}", rc);
            co_return boost::system::error_code(
                ECONNREFUSED, boost::system::system_category());
        }

        // Connect to remote host
        auto ec = co_await connectToHost();
        if (ec)
        {
            co_return ec;
        }

        // Authenticate
        ec = authenticate();
        if (ec)
        {
            co_return ec;
        }

        // Set non-blocking mode
        libssh2_session_set_blocking(sshSession.get(), 0);

        connected = true;

        LOG_INFO("SSH client connected successfully to {}@{}:{}",
                 config.remoteUser, config.remoteHost, config.remotePort);
        co_return boost::system::error_code{};
    }

    /**
     * @brief Close the SSH connection
     */
    void close()
    {
        connected = false;

        // Close SSH socket
        if (sshSocket.is_open())
        {
            boost::system::error_code ec;
            sshSocket.close(ec);
            if (ec)
            {
                LOG_WARNING("Error closing SSH socket: {}", ec.message());
            }
        }

        // RAII wrappers automatically clean up resources
        sshSession.reset();

        LOG_INFO("SSH client closed");
    }

    /**
     * @brief Async read from SSH channel
     * @param channel SSH channel to read from
     * @param buffer Buffer to read into
     * @return Pair of error_code and bytes read
     */
    net::awaitable<std::pair<boost::system::error_code, std::size_t>> read(
        LIBSSH2_CHANNEL* channel, net::mutable_buffer buffer)
    {
        // Wait for SSH socket to become readable
        auto [wait_ec] = co_await sshSocket.async_wait(
            tcp::socket::wait_read,
            boost::asio::as_tuple(boost::asio::use_awaitable));

        if (wait_ec)
        {
            co_return std::make_pair(wait_ec, 0);
        }

        // Read from SSH channel
        char* data = static_cast<char*>(buffer.data());
        size_t size = buffer.size();

        int rc = libssh2_channel_read(channel, data, size);

        if (rc > 0)
        {
            co_return std::make_pair(boost::system::error_code{},
                                     static_cast<size_t>(rc));
        }
        else if (rc == LIBSSH2_ERROR_EAGAIN)
        {
            // Would block, return 0 bytes
            co_return std::make_pair(boost::system::error_code{}, 0);
        }
        else if (rc == 0)
        {
            // EOF
            co_return std::make_pair(boost::asio::error::eof, 0);
        }
        else
        {
            // Error
            LOG_ERROR("SSH channel read failed: {}", rc);
            co_return std::make_pair(boost::system::error_code(
                                         EIO, boost::system::system_category()),
                                     0);
        }
    }

    /**
     * @brief Async write to SSH channel
     * @param channel SSH channel to write to
     * @param buffer Buffer to write from
     * @return Pair of error_code and bytes written
     */
    net::awaitable<std::pair<boost::system::error_code, std::size_t>> write(
        LIBSSH2_CHANNEL* channel, net::const_buffer buffer)
    {
        const char* data = static_cast<const char*>(buffer.data());
        size_t size = buffer.size();
        size_t nwritten = 0;

        while (nwritten < size && connected)
        {
            int rc = libssh2_channel_write(channel, data + nwritten,
                                           size - nwritten);
            if (rc < 0)
            {
                if (rc == LIBSSH2_ERROR_EAGAIN)
                {
                    // Wait a bit and retry
                    using namespace std::chrono_literals;
                    co_await waitFor(executor, 10ms);
                    continue;
                }
                LOG_ERROR("SSH channel write failed: {}", rc);
                co_return std::make_pair(
                    boost::system::error_code(EIO,
                                              boost::system::system_category()),
                    nwritten);
            }
            nwritten += rc;
        }

        co_return std::make_pair(boost::system::error_code{}, nwritten);
    }

    /**
     * @brief Check if client is connected
     */
    bool isOpen() const
    {
        return connected && sshSocket.is_open();
    }

    /**
     * @brief Get connection info string
     */
    std::string getConnectionInfo() const
    {
        return config.remoteUser + "@" + config.remoteHost + ":" +
               std::to_string(config.remotePort);
    }

    /**
     * @brief Get the underlying TCP socket
     */
    tcp::socket& socket()
    {
        return sshSocket;
    }

    /**
     * @brief Get the SSH session
     */
    LIBSSH2_SESSION* session()
    {
        return sshSession.get();
    }

    /**
     * @brief Get the executor
     */
    net::any_io_executor& getExecutor()
    {
        return executor;
    }

  private:
    /**
     * @brief Connect to remote host using Boost.Asio TCP socket
     */
    net::awaitable<boost::system::error_code> connectToHost()
    {
        try
        {
            // Resolve hostname using Boost.Asio resolver
            tcp::resolver resolver(executor);
            std::string portStr = std::to_string(config.remotePort);

            boost::system::error_code ec;
            auto endpoints = resolver.resolve(config.remoteHost, portStr, ec);

            if (ec)
            {
                LOG_ERROR("Failed to resolve host {}: {}", config.remoteHost,
                          ec.message());
                co_return ec;
            }

            // Connect using Boost.Asio
            net::connect(sshSocket, endpoints, ec);

            if (ec)
            {
                LOG_ERROR("Failed to connect to {}:{}: {}", config.remoteHost,
                          config.remotePort, ec.message());
                co_return ec;
            }

            LOG_INFO("Connected to {}:{}", config.remoteHost,
                     config.remotePort);

            // Create SSH session
            sshSession.reset(libssh2_session_init());
            if (!sshSession)
            {
                LOG_ERROR("Failed to create SSH session");
                co_return boost::system::error_code(
                    ENOMEM, boost::system::system_category());
            }

            // Start SSH handshake using socket's native handle
            int rc = libssh2_session_handshake(sshSession.get(),
                                               sshSocket.native_handle());
            if (rc != 0)
            {
                char* errmsg;
                libssh2_session_last_error(sshSession.get(), &errmsg, nullptr,
                                           0);
                LOG_ERROR("SSH handshake failed: {} ({})", errmsg, rc);
                co_return boost::system::error_code(
                    ECONNREFUSED, boost::system::system_category());
            }

            LOG_INFO("SSH handshake completed");
            co_return boost::system::error_code{};
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception in connectToHost: {}", e.what());
            co_return boost::system::error_code(
                ECONNREFUSED, boost::system::system_category());
        }
    }

    /**
     * @brief Authenticate with remote host
     */
    // Keyboard-interactive authentication callback
    // Uses thread_local to pass instance pointer to static callback
    static thread_local SSHClient* currentInstance;

    static void kbdintCallback(
        const char* name, int name_len, const char* instruction,
        int instruction_len, int num_prompts,
        const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
        LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses, void** abstract)
    {
        if (!currentInstance)
        {
            LOG_ERROR("kbdintCallback: currentInstance is null");
            return;
        }

        LOG_DEBUG("kbdintCallback: num_prompts={}", num_prompts);

        // Respond to all prompts with the password
        for (int i = 0; i < num_prompts; i++)
        {
            if (prompts[i].text)
            {
                LOG_DEBUG(
                    "kbdintCallback: prompt[{}]={}", i,
                    std::string(reinterpret_cast<const char*>(prompts[i].text),
                                prompts[i].length));
            }

            responses[i].text =
                strdup(currentInstance->config.remotePassword.c_str());
            responses[i].length =
                currentInstance->config.remotePassword.length();

            LOG_DEBUG("kbdintCallback: responding with password length={}",
                      responses[i].length);
        }
    }

    boost::system::error_code authenticate()
    {
        int rc;

        // Try keyboard-interactive authentication first if password is provided
        // This is required for SSH servers with "KbdInteractiveAuthentication
        // yes" and "PasswordAuthentication no" (common in OpenBMC)
        if (!config.remotePassword.empty())
        {
            LOG_INFO(
                "Attempting keyboard-interactive authentication for user {}",
                config.remoteUser);

            // Store 'this' pointer for callback access via thread_local
            currentInstance = this;

            rc = libssh2_userauth_keyboard_interactive_ex(
                sshSession.get(), config.remoteUser.c_str(),
                config.remoteUser.length(), &kbdintCallback);

            // Clear the instance pointer
            currentInstance = nullptr;

            if (rc == 0)
            {
                LOG_INFO("Keyboard-interactive authentication successful");
                return boost::system::error_code{};
            }

            char* errmsg;
            libssh2_session_last_error(sshSession.get(), &errmsg, nullptr, 0);
            LOG_WARNING("Keyboard-interactive authentication failed: {} ({})",
                        errmsg, rc);

            // Try simple password authentication as fallback
            LOG_INFO("Attempting password authentication for user {}",
                     config.remoteUser);
            rc = libssh2_userauth_password(sshSession.get(),
                                           config.remoteUser.c_str(),
                                           config.remotePassword.c_str());
            if (rc == 0)
            {
                LOG_INFO("Password authentication successful");
                return boost::system::error_code{};
            }

            libssh2_session_last_error(sshSession.get(), &errmsg, nullptr, 0);
            LOG_WARNING("Password authentication failed: {} ({})", errmsg, rc);
        }

        // Try public key authentication if key path is provided
        if (!config.sshKeyPath.empty())
        {
            LOG_INFO("Attempting public key authentication with key: {}",
                     config.sshKeyPath);

            std::string pubKeyPath = config.sshKeyPath + ".pub";
            const char* passphrase = config.sshKeyPassphrase.empty()
                                         ? nullptr
                                         : config.sshKeyPassphrase.c_str();

            rc = libssh2_userauth_publickey_fromfile(
                sshSession.get(), config.remoteUser.c_str(), pubKeyPath.c_str(),
                config.sshKeyPath.c_str(), passphrase);

            if (rc == 0)
            {
                LOG_INFO("Public key authentication successful");
                return boost::system::error_code{};
            }

            char* errmsg;
            libssh2_session_last_error(sshSession.get(), &errmsg, nullptr, 0);
            LOG_ERROR("Public key authentication failed: {} ({})", errmsg, rc);
        }

        LOG_ERROR("All authentication methods failed");
        return boost::system::error_code(EACCES,
                                         boost::system::system_category());
    }

    SshConfig config;
    net::any_io_executor executor;
    tcp::socket sshSocket;
    Ssh2SessionPtr sshSession;
    Ssh2Library ssh2Lib;
    std::atomic<bool> connected;
};

// Define the thread_local static member
thread_local SSHClient* SSHClient::currentInstance = nullptr;

} // namespace NSNAME
