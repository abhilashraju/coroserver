/**
 * @file telnet_tls_server.cpp
 * @brief Telnet-over-TLS console gateway ("telnets", TCP 992).
 *
 * Flow per connection:
 *   TLS handshake -> telnet option negotiation -> PAM login ->
 *   console menu (from obmc-console-multi.conf) -> bridge to the chosen
 *   console_server unix socket.
 *
 * Usage:
 *   telnet_tls_server [--bind 127.0.0.1] [--port 992]
 *                     [--cert /etc/ssl/private/server-cert.pem]
 *                     [--key  /etc/ssl/private/server-key.pem]
 *                     [--config /run/obmc-console/obmc-console-multi.conf]
 *                     [--pam-service telnet-tls] [--group hostconsole]
 *
 * Press Ctrl-] inside a console session to return to the menu.
 */
#include "command_line_parser.hpp"
#include "console_config.hpp"
#include "logger.hpp"
#include "pam_auth.hpp"
#include "tcp_server.hpp"
#include "telnet_protocol.hpp"
#include "unix_client.hpp"
#include "when_all.hpp"

#include <openssl/ssl.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

using namespace NSNAME;
namespace tn = NSNAME::telnet;

namespace
{
constexpr char escapeChar = 0x1d; // Ctrl-]
constexpr int maxLoginAttempts = 3;
constexpr size_t maxLineLength = 128;

struct Options
{
    std::string configPath;
    std::string pamService = "telnet-tls";
    std::string group; // optional extra authorization
    size_t maxSessions = 8;
    size_t active = 0;
};

struct ConsoleEntry
{
    std::string name;
    std::string socketPath;
};

/// Restrict the context to TLS >= 1.2 and AEAD cipher suites.
void hardenContext(ssl::context& ctx)
{
    ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 |
                    ssl::context::no_sslv3 | ssl::context::no_tlsv1 |
                    ssl::context::no_tlsv1_1 | ssl::context::single_dh_use);
    SSL_CTX* native = ctx.native_handle();
    SSL_CTX_set_min_proto_version(native, TLS1_2_VERSION);
    SSL_CTX_set_cipher_list(
        native, "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
                "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
                "DHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384");
    SSL_CTX_set_ciphersuites(native,
                             "TLS_AES_256_GCM_SHA384:TLS_AES_128_GCM_SHA256");
}

/// Consoles offered in the menu. IBM i emulator consoles speak VSLIP/5250,
/// which is not usable through a plain telnet terminal, so they are skipped.
std::vector<ConsoleEntry> loadConsoles(const std::string& path)
{
    std::vector<ConsoleEntry> out;
    auto cfg = ConsoleConfig::loadFromFile(path);
    if (!cfg)
    {
        LOG_ERROR("Cannot read console config {}", path);
        return out;
    }
    for (const auto& dev : cfg->devices)
    {
        if (dev.getDeviceTypeString() == "IBMI_EMULATOR")
        {
            continue;
        }
        out.push_back({dev.getName(), dev.getSocketPath()});
    }
    std::ranges::sort(out, {}, &ConsoleEntry::name);
    return out;
}

class Session
{
  public:
    using Stream = ssl::stream<tcp::socket>;

    Session(std::shared_ptr<Stream> socket, const Options& opts) :
        socket_(std::move(socket)), opts_(opts),
        timer_(std::make_shared<net::steady_timer>(socket_->get_executor())),
        streamer_(socket_, timer_), negotiator_(makePolicy())
    {}
    ~Session()
    {
        timer_->cancel();
    }

    net::awaitable<void> run()
    {
        co_await send(initialNegotiation());

        auto user = co_await login();
        if (!user)
        {
            co_return;
        }
        LOG_INFO("User '{}' logged in from {}", *user, remote_);

        while (true)
        {
            auto consoles = loadConsoles(opts_.configPath);
            if (consoles.empty())
            {
                co_await send("\r\nNo consoles available.\r\n");
                break;
            }
            co_await send(menuText(consoles));
            auto line = co_await readLine(true);
            if (!line || *line == "q" || *line == "quit" || *line == "exit")
            {
                break;
            }
            size_t index = 0;
            auto [ptr, ec] = std::from_chars(
                line->data(), line->data() + line->size(), index);
            if (ec != std::errc{} || ptr != line->data() + line->size() ||
                index < 1 || index > consoles.size())
            {
                co_await send("Invalid selection.\r\n");
                continue;
            }
            const auto& chosen = consoles[index - 1];
            LOG_INFO("User '{}' opened console '{}'", *user, chosen.name);
            auto end = co_await bridge(chosen);
            LOG_INFO("User '{}' left console '{}'", *user, chosen.name);
            if (end == RelayEnd::clientClosed)
            {
                co_return;
            }
        }
        co_await send("\r\nGoodbye.\r\n");
    }

    void setRemote(std::string remote)
    {
        remote_ = std::move(remote);
    }

  private:
    enum class RelayEnd
    {
        none,
        escape,
        consoleClosed,
        clientClosed
    };

    static tn::Policy makePolicy()
    {
        tn::Policy p;
        p.local.set(tn::opt::echo);
        p.local.set(tn::opt::suppressGoAhead);
        p.remote.set(tn::opt::suppressGoAhead);
        p.remote.set(tn::opt::naws);
        p.remote.set(tn::opt::terminalType);
        return p;
    }

    std::string initialNegotiation()
    {
        namespace c = tn::cmd;
        namespace o = tn::opt;
        std::string out;
        out += negotiator_.request(c::will, o::echo);
        out += negotiator_.request(c::will, o::suppressGoAhead);
        out += negotiator_.request(c::doo, o::suppressGoAhead);
        out += negotiator_.request(c::doo, o::naws);
        out += negotiator_.request(c::doo, o::terminalType);
        return out;
    }

    // ── Output ───────────────────────────────────────────────────────────
    // All TLS writes go through here so concurrent coroutines (relay output
    // and negotiation replies) never interleave or overlap.
    net::awaitable<bool> send(std::string data)
    {
        outQueue_ += data;
        if (writing_)
        {
            co_return true; // the active writer will drain the queue
        }
        writing_ = true;
        boost::system::error_code ec;
        while (!outQueue_.empty() && !ec)
        {
            std::string chunk;
            chunk.swap(outQueue_);
            co_await net::async_write(
                *socket_, net::buffer(chunk),
                net::redirect_error(net::use_awaitable, ec));
        }
        outQueue_.clear();
        writing_ = false;
        co_return !ec;
    }

    // ── Input ────────────────────────────────────────────────────────────
    net::awaitable<void> onEvent(const tn::Event& event)
    {
        if (const auto* n = std::get_if<tn::Negotiation>(&event))
        {
            auto reply = negotiator_.handle(*n);
            if (!reply.empty())
            {
                co_await send(std::move(reply));
            }
            if (n->verb == tn::cmd::will &&
                n->option == tn::opt::terminalType &&
                negotiator_.remoteEnabled(tn::opt::terminalType) &&
                !terminalTypeRequested_)
            {
                terminalTypeRequested_ = true;
                co_await send(tn::terminalTypeSendFrame());
            }
        }
        else if (const auto* sb = std::get_if<tn::Subnegotiation>(&event))
        {
            if (sb->option == tn::opt::naws)
            {
                if (auto size = tn::parseNaws(sb->payload))
                {
                    // Console backends have no resize channel; record only.
                    width_ = size->first;
                    height_ = size->second;
                    LOG_DEBUG("NAWS {}x{}", width_, height_);
                }
            }
            else if (sb->option == tn::opt::terminalType)
            {
                if (auto name = tn::parseTerminalTypeIs(sb->payload))
                {
                    LOG_DEBUG("Terminal type reported ({} bytes)",
                              name->size());
                }
            }
        }
    }

    /// Read at least one application byte into @p app.
    net::awaitable<boost::system::error_code> readApp(std::string& app,
                                                      bool timed)
    {
        std::array<char, 1024> buf;
        while (true)
        {
            auto [ec, n] = co_await streamer_.read(net::buffer(buf), timed);
            if (ec)
            {
                co_return ec;
            }
            std::string decoded;
            std::vector<tn::Event> events;
            decoder_.feed(std::string_view(buf.data(), n), decoded, events);
            for (const auto& e : events)
            {
                co_await onEvent(e);
            }
            // Collapse CR LF to CR so a pty does not see two line ends.
            for (char c : decoded)
            {
                if (c == '\n' && lastCr_)
                {
                    lastCr_ = false;
                    continue;
                }
                lastCr_ = (c == '\r');
                app.push_back(c);
            }
            if (!app.empty())
            {
                co_return boost::system::error_code{};
            }
        }
    }

    net::awaitable<std::pair<boost::system::error_code, char>> nextByte(
        bool timed)
    {
        if (pending_.empty())
        {
            auto ec = co_await readApp(pending_, timed);
            if (ec)
            {
                co_return std::make_pair(ec, '\0');
            }
        }
        char c = pending_.front();
        pending_.erase(0, 1);
        co_return std::make_pair(boost::system::error_code{}, c);
    }

    /// Line editor. Echo is done here because we hold the telnet ECHO option.
    net::awaitable<std::optional<std::string>> readLine(bool echo)
    {
        std::string line;
        while (true)
        {
            auto [ec, c] = co_await nextByte(true);
            if (ec)
            {
                co_return std::nullopt;
            }
            auto u = static_cast<unsigned char>(c);
            if (c == '\r' || c == '\n')
            {
                co_await send("\r\n");
                co_return line;
            }
            if (u == 0x03 || u == 0x04) // Ctrl-C / Ctrl-D
            {
                co_return std::nullopt;
            }
            if (u == 0x7f || u == 0x08)
            {
                if (!line.empty())
                {
                    line.pop_back();
                    if (echo)
                    {
                        co_await send("\b \b");
                    }
                }
                continue;
            }
            if (u < 0x20 || u >= 0x7f || line.size() >= maxLineLength)
            {
                continue;
            }
            line.push_back(c);
            if (echo)
            {
                co_await send(std::string(1, c));
            }
        }
    }

    // ── Login ────────────────────────────────────────────────────────────
    net::awaitable<std::optional<std::string>> login()
    {
        for (int attempt = 0; attempt < maxLoginAttempts; ++attempt)
        {
            co_await send("\r\nlogin: ");
            auto user = co_await readLine(true);
            if (!user)
            {
                co_return std::nullopt;
            }
            co_await send("Password: ");
            auto password = co_await readLine(false);
            if (!password)
            {
                co_return std::nullopt;
            }

            bool ok = false;
            if (!user->empty())
            {
                ok = co_await pamauth::authenticate(socket_->get_executor(),
                                                    opts_.pamService, *user,
                                                    std::move(*password));
                if (ok && !opts_.group.empty() &&
                    !pamauth::userInGroup(*user, opts_.group))
                {
                    ok = false;
                }
            }
            if (ok)
            {
                co_return user;
            }
            LOG_WARNING("Failed login attempt from {}", remote_);
            // Slow down guessing.
            net::steady_timer delay(socket_->get_executor());
            delay.expires_after(std::chrono::seconds(2));
            boost::system::error_code ignored;
            co_await delay.async_wait(
                net::redirect_error(net::use_awaitable, ignored));
            co_await send("Login incorrect\r\n");
        }
        co_return std::nullopt;
    }

    // ── Menu / bridge ────────────────────────────────────────────────────
    static std::string menuText(const std::vector<ConsoleEntry>& consoles)
    {
        std::string out = "\r\nAvailable consoles:\r\n";
        for (size_t i = 0; i < consoles.size(); ++i)
        {
            out += std::format("  {}) {}\r\n", i + 1, consoles[i].name);
        }
        out += std::format("Select console [1-{}] or q to quit: ",
                           consoles.size());
        return out;
    }

    net::awaitable<RelayEnd> bridge(const ConsoleEntry& entry)
    {
        UnixClientPlain console(socket_->get_executor());
        auto ec = co_await console.connect(entry.socketPath);
        if (ec)
        {
            co_await send(
                std::format("Cannot connect to console '{}'.\r\n", entry.name));
            co_return RelayEnd::escape; // back to the menu
        }
        co_await send(
            std::format("\r\nConnected to '{}'. Press Ctrl-] to return to "
                        "the menu.\r\n",
                        entry.name));

        end_ = RelayEnd::none;
        auto closeConsole = [&console] {
            boost::system::error_code ignored;
            console.stream().close(ignored);
        };

        auto toConsole = [&]() -> net::awaitable<void> {
            while (end_ == RelayEnd::none)
            {
                if (pending_.empty())
                {
                    auto rc = co_await readApp(pending_, false);
                    if (rc)
                    {
                        if (end_ == RelayEnd::none)
                        {
                            end_ = RelayEnd::clientClosed;
                        }
                        break;
                    }
                }
                if (end_ != RelayEnd::none)
                {
                    break;
                }
                std::string app;
                app.swap(pending_);
                std::string out;
                bool escape = false;
                for (char c : app)
                {
                    if (c == escapeChar)
                    {
                        escape = true;
                        break;
                    }
                    out.push_back(c);
                }
                if (!out.empty())
                {
                    boost::system::error_code wec;
                    co_await net::async_write(
                        console.stream(), net::buffer(out),
                        net::redirect_error(net::use_awaitable, wec));
                    if (wec)
                    {
                        if (end_ == RelayEnd::none)
                        {
                            end_ = RelayEnd::consoleClosed;
                        }
                        break;
                    }
                }
                if (escape)
                {
                    if (end_ == RelayEnd::none)
                    {
                        end_ = RelayEnd::escape;
                    }
                    break;
                }
            }
            closeConsole(); // unblocks toClient
        };

        auto toClient = [&]() -> net::awaitable<void> {
            std::array<char, 4096> buf;
            auto cs = console.streamer();
            while (end_ == RelayEnd::none)
            {
                auto [rec, n] = co_await cs.read(net::buffer(buf), false);
                if (rec || end_ != RelayEnd::none)
                {
                    break;
                }
                if (!co_await send(
                        tn::escapeIac(std::string_view(buf.data(), n))))
                {
                    end_ = RelayEnd::clientClosed;
                    break;
                }
            }
            if (end_ == RelayEnd::none)
            {
                end_ = RelayEnd::consoleClosed;
                // Wake toConsole if it is blocked reading from the client.
                boost::system::error_code ignored;
                socket_->lowest_layer().cancel(ignored);
            }
            closeConsole();
        };

        co_await when_all(toConsole(), toClient());
        closeConsole();

        if (end_ == RelayEnd::consoleClosed)
        {
            co_await send("\r\n[Console closed]\r\n");
        }
        else if (end_ == RelayEnd::escape)
        {
            co_await send("\r\n");
        }
        co_return end_;
    }

    std::shared_ptr<Stream> socket_;
    const Options& opts_;
    std::shared_ptr<net::steady_timer> timer_;
    TimedStreamer<Stream> streamer_;
    tn::Decoder decoder_;
    tn::Negotiator negotiator_;

    std::string pending_;  // decoded input not yet consumed
    std::string outQueue_; // output waiting for the single TLS writer
    std::string remote_ = "unknown";
    RelayEnd end_ = RelayEnd::none;
    bool writing_ = false;
    bool lastCr_ = false;
    bool terminalTypeRequested_ = false;
    uint16_t width_ = 0;
    uint16_t height_ = 0;
};

net::awaitable<void> runSession(std::shared_ptr<Session::Stream> socket,
                                Options& opts)
{
    if (opts.active >= opts.maxSessions)
    {
        LOG_WARNING("Rejecting connection: session limit reached");
        co_return;
    }
    struct Guard
    {
        size_t& n;
        explicit Guard(size_t& n) : n(n)
        {
            ++n;
        }
        ~Guard()
        {
            --n;
        }
    } guard(opts.active);

    Session session(socket, opts);
    boost::system::error_code ec;
    auto ep = socket->next_layer().remote_endpoint(ec);
    if (!ec)
    {
        session.setRemote(ep.address().to_string());
    }
    co_await session.run();
}
} // namespace

int main(int argc, const char* argv[])
{
    try
    {
        std::signal(SIGPIPE, SIG_IGN);

        auto [bindOpt, port, cert, key, config, pamService, group] = getArgs(
            parseCommandline(argc, argv), "--bind,-b", "--port,-p", "--cert,-c",
            "--key,-k", "--config", "--pam-service", "--group");

        Options opts;
        if (config)
        {
            opts.configPath = std::string(*config);
        }
        else if (std::filesystem::exists(
                     "/run/obmc-console/obmc-console-multi.conf"))
        {
            opts.configPath = "/run/obmc-console/obmc-console-multi.conf";
        }
        else
        {
            opts.configPath = "/etc/obmc-console-multi.conf";
        }
        if (pamService)
        {
            opts.pamService = std::string(*pamService);
        }
        if (group)
        {
            opts.group = std::string(*group);
        }

        std::string bindAddress = std::string(bindOpt.value_or("127.0.0.1"));
        int portNumber = 992;
        if (port)
        {
            auto sv = *port;
            auto [p, ec] =
                std::from_chars(sv.data(), sv.data() + sv.size(), portNumber);
            if (ec != std::errc{} || p != sv.data() + sv.size() ||
                portNumber < 1 || portNumber > 65535)
            {
                LOG_ERROR("Invalid --port");
                return EXIT_FAILURE;
            }
        }

        net::io_context io;
        ssl::context sslContext(ssl::context::tls_server);
        hardenContext(sslContext);
        sslContext.use_certificate_chain_file(
            std::string(cert.value_or("/etc/ssl/certs/https/server.mtls.pem")));
        sslContext.use_private_key_file(
            std::string(key.value_or("/etc/ssl/private/server.mtls.key")),
            ssl::context::pem);

        TcpStreamType acceptor(io.get_executor(), bindAddress,
                               static_cast<short>(portNumber), sslContext);
        auto router = [&opts](std::shared_ptr<Session::Stream> socket) {
            return runSession(std::move(socket), opts);
        };
        TcpServer server(io.get_executor(), acceptor, router);

        net::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&io](const boost::system::error_code&, int) {
            LOG_INFO("Shutting down");
            io.stop();
        });

        LOG_INFO("telnet_tls_server listening on {}:{} (console config: {})",
                 bindAddress, portNumber, opts.configPath);
        io.run();
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Fatal: {}", e.what());
        return EXIT_FAILURE;
    }
}
