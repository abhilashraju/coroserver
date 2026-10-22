/**
 * @file telnet_tls_client.cpp
 * @brief Companion client for telnet_tls_server (telnet over TLS).
 *
 * Usage:
 *   telnet_tls_client --host <host> [--port 992] [--cafile ca.pem]
 *                     [--insecure 1]
 *
 * Server certificates are verified (chain + host name) against --cafile or
 * the system trust store. --insecure 1 disables verification and is meant
 * for self-signed test setups only.
 *
 * Local escape: Enter ~ .  disconnects. Ctrl-] is passed to the server, which
 * uses it to leave a console session and return to its menu.
 */
#include "command_line_parser.hpp"
#include "logger.hpp"
#include "tcp_client.hpp"
#include "telnet_protocol.hpp"

#include <fcntl.h>
#include <openssl/ssl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/signal_set.hpp>

#include <array>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <string>
#include <vector>

using namespace NSNAME;
namespace tn = NSNAME::telnet;

namespace
{
class TerminalManager
{
  public:
    TerminalManager()
    {
        if (tcgetattr(STDIN_FILENO, &orig_) == 0)
        {
            saved_ = true;
        }
    }
    ~TerminalManager()
    {
        restore();
    }
    TerminalManager(const TerminalManager&) = delete;
    TerminalManager& operator=(const TerminalManager&) = delete;

    bool setRawMode()
    {
        if (!saved_)
        {
            return false;
        }
        struct termios raw = orig_;
        cfmakeraw(&raw);
        return tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
    }
    void restore()
    {
        if (saved_)
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &orig_);
            saved_ = false;
        }
    }

  private:
    struct termios orig_{};
    bool saved_ = false;
};

/// Detects "Enter ~ ." typed at the start of a line.
class EscapeDetector
{
  public:
    bool feed(char c)
    {
        switch (state_)
        {
            case State::lineStart:
                state_ = (c == '~') ? State::tilde : next(c);
                break;
            case State::tilde:
                if (c == '.')
                {
                    return true;
                }
                state_ = next(c);
                break;
            case State::normal:
                state_ = next(c);
                break;
        }
        return false;
    }

  private:
    enum class State
    {
        normal,
        lineStart,
        tilde
    };
    static State next(char c)
    {
        return (c == '\r' || c == '\n') ? State::lineStart : State::normal;
    }
    State state_ = State::lineStart;
};

void windowSize(uint16_t& w, uint16_t& h)
{
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
    {
        w = ws.ws_col;
        h = ws.ws_row;
    }
    else
    {
        w = 80;
        h = 24;
    }
}

void writeAll(int fd, const char* data, size_t len)
{
    while (len > 0)
    {
        ssize_t n = ::write(fd, data, len);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EAGAIN)
            {
                continue;
            }
            return;
        }
        data += n;
        len -= static_cast<size_t>(n);
    }
}

class TelnetTlsClient
{
  public:
    TelnetTlsClient(net::io_context& io, ssl::context& ctx) :
        io_(io), client_(io.get_executor(), ctx), stdin_(io),
        negotiator_(makePolicy())
    {}

    TcpClient& client()
    {
        return client_;
    }

    net::awaitable<void> run(const std::string& host, const std::string& port,
                             bool insecure)
    {
        if (!insecure)
        {
            client_.stream().set_verify_callback(
                ssl::host_name_verification(host));
        }
        SSL_set_tlsext_host_name(client_.stream().native_handle(),
                                 host.c_str());

        auto ec = co_await client_.connect(host, port);
        if (ec)
        {
            LOG_ERROR("Connection failed: {}", ec.message());
            io_.stop();
            co_return;
        }
        // connect() leaves its handshake timeout armed; an interactive session
        // may be idle for longer, so disarm it.
        client_.streamer().timer->cancel();

        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (flags >= 0)
        {
            fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        }
        stdin_.assign(STDIN_FILENO);
        savedFlags_ = flags;

        if (!terminal_.setRawMode())
        {
            LOG_WARNING("stdin is not a terminal; running without raw mode");
        }

        net::co_spawn(io_, watchResize(), net::detached);
        net::co_spawn(io_, readStdin(), net::detached);
        co_await readSocket();
        shutdown();
    }

    void shutdown()
    {
        if (stdin_.is_open())
        {
            stdin_.release();
        }
        if (savedFlags_ >= 0)
        {
            fcntl(STDIN_FILENO, F_SETFL, savedFlags_);
            savedFlags_ = -1;
        }
        terminal_.restore();
        client_.close();
        io_.stop();
    }

  private:
    static tn::Policy makePolicy()
    {
        tn::Policy p;
        p.local.set(tn::opt::naws);
        p.local.set(tn::opt::terminalType);
        p.local.set(tn::opt::suppressGoAhead);
        p.remote.set(tn::opt::echo);
        p.remote.set(tn::opt::suppressGoAhead);
        return p;
    }

    // Single writer: negotiation replies and user input must not overlap.
    net::awaitable<bool> send(std::string data)
    {
        outQueue_ += data;
        if (writing_)
        {
            co_return true;
        }
        writing_ = true;
        boost::system::error_code ec;
        while (!outQueue_.empty() && !ec)
        {
            std::string chunk;
            chunk.swap(outQueue_);
            co_await net::async_write(
                client_.stream(), net::buffer(chunk),
                net::redirect_error(net::use_awaitable, ec));
        }
        outQueue_.clear();
        writing_ = false;
        co_return !ec;
    }

    net::awaitable<void> onEvent(const tn::Event& event)
    {
        if (const auto* n = std::get_if<tn::Negotiation>(&event))
        {
            auto reply = negotiator_.handle(*n);
            if (!reply.empty())
            {
                co_await send(std::move(reply));
            }
            if (n->option == tn::opt::naws &&
                negotiator_.localEnabled(tn::opt::naws) && !nawsSent_)
            {
                nawsSent_ = true;
                co_await sendWindowSize();
            }
        }
        else if (const auto* sb = std::get_if<tn::Subnegotiation>(&event))
        {
            if (sb->option == tn::opt::terminalType && !sb->payload.empty() &&
                static_cast<uint8_t>(sb->payload[0]) == tn::ttypeSend &&
                negotiator_.localEnabled(tn::opt::terminalType))
            {
                const char* term = std::getenv("TERM");
                co_await send(tn::terminalTypeIsFrame(
                    (term != nullptr && *term != '\0') ? term : "xterm"));
            }
        }
    }

    net::awaitable<void> sendWindowSize()
    {
        if (!negotiator_.localEnabled(tn::opt::naws))
        {
            co_return;
        }
        uint16_t w = 0;
        uint16_t h = 0;
        windowSize(w, h);
        co_await send(tn::nawsFrame(w, h));
    }

    net::awaitable<void> watchResize()
    {
        net::signal_set signals(io_, SIGWINCH);
        while (true)
        {
            boost::system::error_code ec;
            co_await signals.async_wait(
                net::redirect_error(net::use_awaitable, ec));
            if (ec)
            {
                co_return;
            }
            co_await sendWindowSize();
        }
    }

    net::awaitable<void> readSocket()
    {
        std::array<char, 4096> buf;
        auto streamer = client_.streamer();
        while (true)
        {
            // No idle timeout: an interactive session can be quiet for long.
            auto [ec, n] = co_await streamer.read(net::buffer(buf), false);
            if (ec)
            {
                if (ec != net::error::eof &&
                    ec != net::error::operation_aborted &&
                    ec != ssl::error::stream_truncated)
                {
                    LOG_ERROR("Connection error: {}", ec.message());
                }
                co_return;
            }
            std::string data;
            std::vector<tn::Event> events;
            decoder_.feed(std::string_view(buf.data(), n), data, events);
            if (!data.empty())
            {
                writeAll(STDOUT_FILENO, data.data(), data.size());
            }
            for (const auto& e : events)
            {
                co_await onEvent(e);
            }
        }
    }

    net::awaitable<void> readStdin()
    {
        std::array<char, 1024> buf;
        EscapeDetector escape;
        while (true)
        {
            boost::system::error_code ec;
            auto n = co_await stdin_.async_read_some(
                net::buffer(buf), net::redirect_error(net::use_awaitable, ec));
            if (ec || n == 0)
            {
                break;
            }
            bool quit = false;
            for (size_t i = 0; i < n; ++i)
            {
                if (escape.feed(buf[i]))
                {
                    quit = true;
                    n = i; // do not forward the "." (the "~" already went)
                    break;
                }
            }
            if (n > 0 &&
                !co_await send(tn::escapeIac(std::string_view(buf.data(), n))))
            {
                break;
            }
            if (quit)
            {
                break;
            }
        }
        shutdown();
    }

    net::io_context& io_;
    TcpClient client_;
    net::posix::stream_descriptor stdin_;
    TerminalManager terminal_;
    tn::Decoder decoder_;
    tn::Negotiator negotiator_;
    std::string outQueue_;
    int savedFlags_ = -1;
    bool writing_ = false;
    bool nawsSent_ = false;
};
} // namespace

int main(int argc, const char* argv[])
{
    try
    {
        auto [host, port, cafile, insecure] =
            getArgs(parseCommandline(argc, argv), "--host,-h", "--port,-p",
                    "--cafile", "--insecure");
        if (!host)
        {
            std::cerr << "Usage: telnet_tls_client --host <host> [--port 992] "
                         "[--cafile ca.pem] [--insecure 1]\n";
            return EXIT_FAILURE;
        }
        bool skipVerify = insecure && *insecure == "1";

        net::io_context io;
        ssl::context ctx(ssl::context::tls_client);
        ctx.set_options(ssl::context::default_workarounds |
                        ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                        ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1);
        SSL_CTX_set_min_proto_version(ctx.native_handle(), TLS1_2_VERSION);
        if (skipVerify)
        {
            LOG_WARNING("Certificate verification disabled (--insecure)");
            ctx.set_verify_mode(ssl::verify_none);
        }
        else
        {
            ctx.set_verify_mode(ssl::verify_peer);
            if (cafile)
            {
                ctx.load_verify_file(std::string(*cafile));
            }
            else
            {
                ctx.set_default_verify_paths();
            }
        }

        TelnetTlsClient app(io, ctx);
        net::co_spawn(io,
                      app.run(std::string(*host),
                              std::string(port.value_or("992")), skipVerify),
                      [&io](std::exception_ptr e) {
                          if (e)
                          {
                              io.stop();
                          }
                      });
        io.run();
        app.shutdown();
        std::cerr << "\r\nConnection closed.\r\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
