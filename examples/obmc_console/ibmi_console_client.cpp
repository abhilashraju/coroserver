/**
 * @file ibmi_console_client.cpp
 * @brief IBMi 5250 Console Client over VUART0
 *
 * Connects to the Unix socket exposed by console_server for the VUART0
 * channel (/var/run/obmc-console-host.sock by default).
 *
 * Data flow (receive):
 *   Unix socket → VSlipFramer::feed() → Stream5250Parser::process()
 *               → Screen5250 model → ScreenRenderer::render()
 *
 * Data flow (send):
 *   Raw keypress → AID record encode → VSlipFramer::encode()
 *               → Unix socket write
 *
 * Usage:
 *   ibmi_console_client /var/run/obmc-console-host.sock
 *   ibmi_console_client -s /var/run/obmc-console-host.sock
 *
 * Escape sequence: Enter ~ .  (same as console_client) to disconnect.
 */

#include "beastdefs.hpp"
#include "command_line_parser.hpp"
#include "completion_handler.hpp"
#include "logger.hpp"
#include "screen_renderer.hpp"
#include "screen5250.hpp"
#include "stream5250_parser.hpp"
#include "unix_client.hpp"
#include "vslip_framer.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>

#include <csignal>
#include <cstdint>
#include <iostream>
#include <stop_token>
#include <vector>

using namespace NSNAME;

// ---------------------------------------------------------------------------
// Global stop source
// ---------------------------------------------------------------------------
std::stop_source globalStopSource;

void signalHandler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
    {
        LOG_INFO("Shutdown signal received");
        globalStopSource.request_stop();
    }
}

// ---------------------------------------------------------------------------
// TerminalManager — true raw mode so Fn / arrow keys arrive as byte sequences
// ---------------------------------------------------------------------------
class TerminalManager
{
  public:
    TerminalManager()
    {
        if (tcgetattr(STDIN_FILENO, &orig_) == 0)
            saved_ = true;
    }

    ~TerminalManager()
    {
        restore();
    }

    bool setRawMode()
    {
        if (!saved_)
            return false;
        struct termios raw = orig_;
        ::cfmakeraw(&raw);              // true raw: no echo, no signals, no canonical
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
        {
            LOG_ERROR("Failed to set terminal raw mode");
            return false;
        }
        LOG_INFO("Terminal set to raw mode");
        return true;
    }

    void restore()
    {
        if (saved_)
        {
            tcsetattr(STDIN_FILENO, TCSANOW, &orig_);
            saved_ = false;
            LOG_INFO("Terminal restored");
        }
    }

  private:
    struct termios orig_{};
    bool saved_ = false;
};

// ---------------------------------------------------------------------------
// AID key encoder
//
// Encodes a keystroke as a minimal 5250 input record:
//   [AID byte] [cursor row high] [cursor row low] [cursor col high] [cursor col low]
//
// For Enter and function keys the host needs the AID + current cursor position.
// The record is then VSLIP-framed before sending.
// ---------------------------------------------------------------------------
namespace Aid
{
constexpr uint8_t ENTER = 0xF1;
constexpr uint8_t F1    = 0x31;
constexpr uint8_t F2    = 0x32;
constexpr uint8_t F3    = 0x33;
constexpr uint8_t F4    = 0x34;
constexpr uint8_t F5    = 0x35;
constexpr uint8_t F6    = 0x36;
constexpr uint8_t F7    = 0x37;
constexpr uint8_t F8    = 0x38;
constexpr uint8_t F9    = 0x39;
constexpr uint8_t F10   = 0x3A;
constexpr uint8_t F11   = 0x3B;
constexpr uint8_t F12   = 0x3C;
constexpr uint8_t F13   = 0xB1;
constexpr uint8_t F24   = 0xBC;
constexpr uint8_t PA1   = 0x6C; // Page up
constexpr uint8_t PA2   = 0x6E; // Page down
constexpr uint8_t CLEAR = 0xBD;
} // namespace Aid

/// Build a 5250 AID transmission record (5 bytes).
static std::vector<uint8_t> makeAidRecord(uint8_t aid, int row, int col)
{
    // Cursor address packed as two 6-bit values per SBA convention
    uint8_t r = static_cast<uint8_t>(row & 0x3F);
    uint8_t c = static_cast<uint8_t>(col & 0x3F);
    return {aid, r, c};
}

// ---------------------------------------------------------------------------
// IBMi console client
// ---------------------------------------------------------------------------
class IbmiConsoleClient
{
  public:
    IbmiConsoleClient(net::any_io_executor exec,
                      const std::string& socketPath,
                      std::stop_token    stopToken) :
        exec_(exec), socketPath_(socketPath), stopToken_(stopToken),
        client_(exec), stdinStream_(exec)
    {
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (flags >= 0)
            fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        stdinStream_.assign(STDIN_FILENO);
    }

    ~IbmiConsoleClient()
    {
        cleanup();
    }

    net::awaitable<void> run()
    {
        try
        {
            LOG_INFO("Connecting to IBMi console: {}", socketPath_);
            auto ec = co_await client_.connect(socketPath_);
            if (ec)
            {
                LOG_ERROR("Failed to connect: {}", ec.message());
                globalStopSource.request_stop();
                co_return;
            }

            LOG_INFO("Connected. Waiting for 5250 data from VUART0…");
            LOG_INFO("Press Enter ~ . to disconnect");

            if (!termMgr_.setRawMode())
            {
                LOG_ERROR("Failed to set raw terminal mode");
                co_return;
            }

            ScreenRenderer::renderStatusLine("IBMi 5250 — connecting…");

            // Spawn receive coroutine; run send coroutine inline
            boost::asio::co_spawn(
                exec_,
                [this]() -> net::awaitable<void> { co_await receiveLoop(); },
                boost::asio::detached);

            co_await sendLoop();
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception in IbmiConsoleClient::run: {}", e.what());
        }
        cleanup();
    }

    void cleanup()
    {
        if (stdinStream_.is_open())
            stdinStream_.release();

        termMgr_.restore();

        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (flags >= 0)
            fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);

        client_.close();
    }

  private:
    // -----------------------------------------------------------------------
    // Receive: socket bytes → VSLIP decode → 5250 parse → render
    // -----------------------------------------------------------------------
    net::awaitable<void> receiveLoop()
    {
        try
        {
            std::array<uint8_t, 4096> buf{};

            while (!stopToken_.stop_requested())
            {
                auto [ec, n] = co_await client_.read(
                    boost::asio::buffer(buf));

                if (ec)
                {
                    if (ec == boost::asio::error::eof)
                        LOG_INFO("Server closed connection");
                    else if (ec != boost::asio::error::operation_aborted)
                        LOG_ERROR("Socket read error: {}", ec.message());
                    globalStopSource.request_stop();
                    break;
                }

                if (n > 0)
                {
                    std::vector<std::vector<uint8_t>> frames;
                    framer_.feed(std::span<const uint8_t>(buf.data(), n), frames);

                    for (auto& frame : frames)
                    {
                        parser_.process(frame, screen_);
                    }

                    if (!frames.empty())
                    {
                        ScreenRenderer::render(screen_);
                        ScreenRenderer::renderStatusLine(
                            "IBMi 5250  [Enter~. to quit]");
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception in receiveLoop: {}", e.what());
            globalStopSource.request_stop();
        }

        globalStopSource.request_stop();
        stdinStream_.cancel();
    }

    // -----------------------------------------------------------------------
    // Send: raw keypress → AID record → VSLIP encode → socket write
    // -----------------------------------------------------------------------
    net::awaitable<void> sendLoop()
    {
        try
        {
            std::array<uint8_t, 32> buf{};

            // Escape sequence state: newline → tilde → dot = disconnect
            enum class EscState { Normal, AfterNewline, AfterTilde };
            EscState escState = EscState::Normal;

            while (!stopToken_.stop_requested())
            {
                boost::system::error_code ec;
                size_t n = co_await stdinStream_.async_read_some(
                    boost::asio::buffer(buf),
                    boost::asio::redirect_error(
                        boost::asio::use_awaitable, ec));

                if (ec)
                {
                    if (ec != boost::asio::error::operation_aborted)
                        LOG_ERROR("Stdin read error: {}", ec.message());
                    break;
                }

                if (stopToken_.stop_requested())
                    break;

                // Check escape sequence (Enter ~ .)
                for (size_t i = 0; i < n; ++i)
                {
                    uint8_t c = buf[i];
                    switch (escState)
                    {
                        case EscState::Normal:
                            if (c == '\r' || c == '\n')
                                escState = EscState::AfterNewline;
                            break;
                        case EscState::AfterNewline:
                            if (c == '~')      { escState = EscState::AfterTilde; continue; }
                            else               { escState = EscState::Normal; }
                            break;
                        case EscState::AfterTilde:
                            if (c == '.')
                            {
                                LOG_INFO("Escape sequence — disconnecting");
                                globalStopSource.request_stop();
                                co_return;
                            }
                            escState = EscState::Normal;
                            break;
                    }
                }

                if (stopToken_.stop_requested())
                    break;

                // Map raw input to 5250 AID record or pass-through characters
                std::vector<uint8_t> payload = mapInput(
                    std::span<const uint8_t>(buf.data(), n));

                if (!payload.empty())
                {
                    auto frame = VSlipFramer::encode(payload);
                    auto [wec, wb] = co_await client_.write(
                        boost::asio::buffer(frame));
                    if (wec)
                    {
                        LOG_ERROR("Socket write error: {}", wec.message());
                        globalStopSource.request_stop();
                        break;
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception in sendLoop: {}", e.what());
            globalStopSource.request_stop();
        }

        globalStopSource.request_stop();
        client_.close();
    }

    // -----------------------------------------------------------------------
    // Map raw terminal key bytes to a 5250 payload.
    //
    // Single printable ASCII chars are EBCDIC-encoded and sent as character
    // data.  Special escape sequences (ANSI Fn key codes) are mapped to AID
    // records.  Enter / Return always sends an AID_ENTER record.
    // -----------------------------------------------------------------------
    std::vector<uint8_t> mapInput(std::span<const uint8_t> raw)
    {
        if (raw.empty())
            return {};

        // Enter key
        if (raw.size() == 1 && (raw[0] == '\r' || raw[0] == '\n'))
        {
            return makeAidRecord(Aid::ENTER,
                                 screen_.inputRow, screen_.inputCol);
        }

        // ANSI escape sequences for function keys: ESC [ <n> ~
        // ESC [ 1 1 ~ = F1,  ESC [ 1 2 ~ = F2, …  ESC [ 2 4 ~ = F12
        if (raw.size() >= 4 && raw[0] == 0x1B && raw[1] == '[')
        {
            // Try to decode Fn key number
            int fnNum = 0;
            size_t i = 2;
            while (i < raw.size() && raw[i] >= '0' && raw[i] <= '9')
                fnNum = fnNum * 10 + (raw[i++] - '0');

            // Map function key number to AID byte (F1=11…F12=23 in xterm)
            static const uint8_t fnAids[] = {
                Aid::F1,  Aid::F2,  Aid::F3,  Aid::F4,
                Aid::F5,  Aid::F6,  Aid::F7,  Aid::F8,
                Aid::F9,  Aid::F10, Aid::F11, Aid::F12,
            };
            if (fnNum >= 11 && fnNum <= 23)
            {
                int idx = fnNum - 11;
                if (idx < static_cast<int>(std::size(fnAids)))
                    return makeAidRecord(fnAids[idx],
                                         screen_.inputRow, screen_.inputCol);
            }

            // Page up / down
            if (fnNum == 5) // xterm page-up
                return makeAidRecord(Aid::PA1, screen_.inputRow, screen_.inputCol);
            if (fnNum == 6) // xterm page-down
                return makeAidRecord(Aid::PA2, screen_.inputRow, screen_.inputCol);

            return {}; // unhandled escape sequence — drop it
        }

        // Printable ASCII → convert to EBCDIC and send as character data
        // (simple pass-through for typing into input fields)
        std::vector<uint8_t> out;
        out.reserve(raw.size());
        for (uint8_t c : raw)
        {
            if (c >= 0x20 && c <= 0x7E)
                out.push_back(asciiToEbcdic(c));
        }
        return out;
    }

    /// Reverse lookup: ASCII → EBCDIC CP037 (covers printable range only).
    static uint8_t asciiToEbcdic(uint8_t ascii)
    {
        // Brute-force search of the forward table; called only on user keypress
        // so performance is not a concern.
        for (int i = 0; i < 256; ++i)
        {
            if (static_cast<uint8_t>(kEbcdicToAsciiTable[i]) == ascii)
                return static_cast<uint8_t>(i);
        }
        return 0x40; // EBCDIC space as fallback
    }

    // -----------------------------------------------------------------------
    net::any_io_executor   exec_;
    std::string            socketPath_;
    std::stop_token        stopToken_;
    UnixClientPlain        client_;
    boost::asio::posix::stream_descriptor stdinStream_;
    TerminalManager        termMgr_;
    VSlipFramer            framer_;
    Stream5250Parser       parser_;
    Screen5250             screen_;
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
auto makeCompletionHandler(net::io_context& ioContext)
{
    return reactor::makeCompletionHandler(
        "Exception in ibmi_console_client", [&ioContext]() {
            ioContext.stop();
        });
}

int main(int argc, const char* argv[])
{
    try
    {
        std::string socketPath = "/var/run/obmc-console-host.sock";

        // Accept: ibmi_console_client <socket-path>
        //      or ibmi_console_client -s <socket-path>
        //      or ibmi_console_client --socket <socket-path>
        if (argc >= 2)
        {
            std::string arg1 = argv[1];
            if ((arg1 == "-s" || arg1 == "--socket") && argc >= 3)
                socketPath = argv[2];
            else if (arg1[0] != '-')
                socketPath = arg1;
        }

        std::signal(SIGINT,  signalHandler);
        std::signal(SIGTERM, signalHandler);

        net::io_context ioContext;

        auto stopToken = globalStopSource.get_token();

        IbmiConsoleClient client(ioContext.get_executor(), socketPath, stopToken);

        boost::asio::co_spawn(
            ioContext,
            [&client]() -> net::awaitable<void> { co_await client.run(); },
            makeCompletionHandler(ioContext));

        // Stop the io_context when stop is requested from signal handler
        boost::asio::signal_set signals(ioContext, SIGINT, SIGTERM);
        signals.async_wait(
            [&ioContext](const boost::system::error_code&, int) {
                ioContext.stop();
            });

        ioContext.run();
        return EXIT_SUCCESS;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Fatal: {}", e.what());
        return EXIT_FAILURE;
    }
}
