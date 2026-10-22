#pragma once
/**
 * @file pam_auth.hpp
 * @brief Non-blocking PAM username/password authentication.
 *
 * PAM calls block (and may load NSS/LDAP modules), and the project is built
 * with BOOST_ASIO_DISABLE_THREADS, so a worker thread cannot safely hand the
 * result back to the io_context. Instead the authentication runs in a
 * short-lived forked child that reports one byte over a pipe; the parent
 * watches the pipe with asio and never blocks.
 */
#include "logger.hpp"
#include "make_awaitable.hpp"

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/steady_timer.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

namespace NSNAME::pamauth
{

inline constexpr size_t maxUserLen = 64;
inline constexpr size_t maxPasswordLen = 512;

namespace detail
{
struct ConvData
{
    const char* user;
    const char* password;
};

inline int conversation(int numMsg, const struct pam_message** msg,
                        struct pam_response** resp, void* appdata)
{
    if (numMsg <= 0 || numMsg > PAM_MAX_NUM_MSG)
    {
        return PAM_CONV_ERR;
    }
    auto* data = static_cast<ConvData*>(appdata);
    auto* replies = static_cast<struct pam_response*>(
        std::calloc(static_cast<size_t>(numMsg), sizeof(struct pam_response)));
    if (replies == nullptr)
    {
        return PAM_BUF_ERR;
    }
    for (int i = 0; i < numMsg; ++i)
    {
        switch (msg[i]->msg_style)
        {
            case PAM_PROMPT_ECHO_OFF:
                replies[i].resp = strdup(data->password);
                break;
            case PAM_PROMPT_ECHO_ON:
                replies[i].resp = strdup(data->user);
                break;
            case PAM_ERROR_MSG:
            case PAM_TEXT_INFO:
                break;
            default:
                for (int j = 0; j < i; ++j)
                {
                    std::free(replies[j].resp);
                }
                std::free(replies);
                return PAM_CONV_ERR;
        }
    }
    *resp = replies;
    return PAM_SUCCESS;
}

/// Runs in the forked child. Returns true on PAM success.
inline bool authenticateBlocking(const char* service, const char* user,
                                 const char* password)
{
    ConvData data{user, password};
    struct pam_conv conv{conversation, &data};
    pam_handle_t* handle = nullptr;

    int rc = pam_start(service, user, &conv, &handle);
    if (rc != PAM_SUCCESS)
    {
        return false;
    }
    rc = pam_authenticate(handle, PAM_DISALLOW_NULL_AUTHTOK);
    if (rc == PAM_SUCCESS)
    {
        rc = pam_acct_mgmt(handle, 0);
    }
    pam_end(handle, rc);
    return rc == PAM_SUCCESS;
}
} // namespace detail

/// True if @p user is a member of @p group (primary or supplementary).
inline bool userInGroup(const std::string& user, const std::string& group)
{
    struct passwd* pw = getpwnam(user.c_str());
    struct group* gr = getgrnam(group.c_str());
    if (pw == nullptr || gr == nullptr)
    {
        return false;
    }
    if (pw->pw_gid == gr->gr_gid)
    {
        return true;
    }
    for (char** member = gr->gr_mem; member != nullptr && *member != nullptr;
         ++member)
    {
        if (user == *member)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Authenticate @p user / @p password against PAM service @p service
 *        without blocking the io_context.
 *
 * @return true only if pam_authenticate and pam_acct_mgmt both succeed within
 *         @p timeout. Any failure (bad input, fork error, timeout) is false.
 */
inline net::awaitable<bool> authenticate(
    net::any_io_executor exec, std::string service, std::string user,
    std::string password,
    std::chrono::seconds timeout = std::chrono::seconds(30))
{
    if (user.empty() || user.size() > maxUserLen ||
        password.size() > maxPasswordLen ||
        user.find('\0') != std::string::npos ||
        password.find('\0') != std::string::npos)
    {
        co_return false;
    }

    int fds[2];
    if (::pipe(fds) != 0)
    {
        LOG_ERROR("pipe() failed: {}", std::strerror(errno));
        co_return false;
    }

    pid_t pid = ::fork();
    if (pid < 0)
    {
        LOG_ERROR("fork() failed: {}", std::strerror(errno));
        ::close(fds[0]);
        ::close(fds[1]);
        co_return false;
    }
    if (pid == 0)
    {
        // Child: only async-signal-unsafe work is PAM itself; exit via _exit.
        ::close(fds[0]);
        char ok = detail::authenticateBlocking(service.c_str(), user.c_str(),
                                               password.c_str())
                      ? 1
                      : 0;
        ssize_t ignored = ::write(fds[1], &ok, 1);
        (void)ignored;
        ::_exit(0);
    }

    ::close(fds[1]);
    // Do not keep the secret around longer than needed.
    std::fill(password.begin(), password.end(), '\0');

    bool authenticated = false;
    {
        net::posix::stream_descriptor reader(exec, fds[0]);
        net::steady_timer timer(exec);
        timer.expires_after(timeout);
        timer.async_wait([&reader, pid](const boost::system::error_code& ec) {
            if (!ec)
            {
                ::kill(pid, SIGKILL);
                boost::system::error_code ignored;
                reader.cancel(ignored);
            }
        });

        char result = 0;
        boost::system::error_code ec;
        auto n = co_await net::async_read(
            reader, net::buffer(&result, 1),
            net::redirect_error(net::use_awaitable, ec));
        timer.cancel();
        authenticated = !ec && n == 1 && result == 1;
    }

    // Reap the child; it has already written its result and is exiting (or was
    // killed by the timeout above).
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR)
    {}
    co_return authenticated;
}

} // namespace NSNAME::pamauth
