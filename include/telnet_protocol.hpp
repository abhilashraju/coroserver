#pragma once
/**
 * @file telnet_protocol.hpp
 * @brief Minimal RFC 854/855 telnet layer (IAC decoding/encoding and option
 *        negotiation). Independent of sockets and TLS.
 *
 * Supported options: ECHO, SUPPRESS-GO-AHEAD, TERMINAL-TYPE, NAWS.
 */
#include "name_space.hpp"

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace NSNAME::telnet
{

namespace cmd
{
inline constexpr uint8_t se = 240;
inline constexpr uint8_t nop = 241;
inline constexpr uint8_t sb = 250;
inline constexpr uint8_t will = 251;
inline constexpr uint8_t wont = 252;
inline constexpr uint8_t doo = 253; // "do" is a keyword
inline constexpr uint8_t dont = 254;
inline constexpr uint8_t iac = 255;
} // namespace cmd

namespace opt
{
inline constexpr uint8_t echo = 1;
inline constexpr uint8_t suppressGoAhead = 3;
inline constexpr uint8_t terminalType = 24;
inline constexpr uint8_t naws = 31;
} // namespace opt

inline constexpr uint8_t ttypeIs = 0;
inline constexpr uint8_t ttypeSend = 1;

/// Upper bound for a subnegotiation payload; larger ones are truncated.
inline constexpr size_t maxSubnegotiationSize = 1024;

// ─── Events produced by the decoder ──────────────────────────────────────────

struct Negotiation
{
    uint8_t verb;   // cmd::will / wont / doo / dont
    uint8_t option; // opt::*
};

struct Subnegotiation
{
    uint8_t option;
    std::string payload; // IAC-unescaped
};

struct Command
{
    uint8_t code; // any other IAC <code> (NOP, AYT, IP, ...)
};

using Event = std::variant<Negotiation, Subnegotiation, Command>;

// ─── Decoder ─────────────────────────────────────────────────────────────────

/**
 * @brief Incremental IAC stream decoder.
 *
 * feed() may be called with arbitrary chunking: commands split across reads are
 * handled. Application bytes are appended to @p data (IAC IAC becomes a single
 * 0xFF, and the NUL following a CR is dropped per RFC 854). Telnet commands are
 * appended to @p events in arrival order.
 */
class Decoder
{
  public:
    void feed(std::string_view in, std::string& data,
              std::vector<Event>& events)
    {
        for (char ch : in)
        {
            auto c = static_cast<uint8_t>(ch);
            switch (state_)
            {
                case State::data:
                    dataByte(c, data);
                    break;
                case State::cr:
                    if (c == 0)
                    {
                        state_ = State::data; // CR NUL == CR
                    }
                    else
                    {
                        dataByte(c, data);
                    }
                    break;
                case State::iac:
                    onIac(c, data, events);
                    break;
                case State::verb:
                    events.emplace_back(Negotiation{verb_, c});
                    state_ = State::data;
                    break;
                case State::sbOption:
                    sbOption_ = c;
                    sbPayload_.clear();
                    state_ = State::sbData;
                    break;
                case State::sbData:
                    if (c == cmd::iac)
                    {
                        state_ = State::sbIac;
                    }
                    else
                    {
                        sbPush(c);
                    }
                    break;
                case State::sbIac:
                    if (c == cmd::se)
                    {
                        events.emplace_back(
                            Subnegotiation{sbOption_, std::move(sbPayload_)});
                        sbPayload_.clear();
                        state_ = State::data;
                    }
                    else if (c == cmd::iac)
                    {
                        sbPush(c);
                        state_ = State::sbData;
                    }
                    else
                    {
                        // Malformed: IAC inside SB followed by something else.
                        sbPayload_.clear();
                        events.emplace_back(Command{c});
                        state_ = State::data;
                    }
                    break;
            }
        }
    }

  private:
    enum class State
    {
        data,
        cr,
        iac,
        verb,
        sbOption,
        sbData,
        sbIac
    };

    void dataByte(uint8_t c, std::string& data)
    {
        if (c == cmd::iac)
        {
            state_ = State::iac;
            return;
        }
        data.push_back(static_cast<char>(c));
        state_ = (c == '\r') ? State::cr : State::data;
    }

    void onIac(uint8_t c, std::string& data, std::vector<Event>& events)
    {
        if (c == cmd::iac)
        {
            data.push_back(static_cast<char>(cmd::iac));
            state_ = State::data;
        }
        else if (c >= cmd::will && c <= cmd::dont)
        {
            verb_ = c;
            state_ = State::verb;
        }
        else if (c == cmd::sb)
        {
            state_ = State::sbOption;
        }
        else
        {
            events.emplace_back(Command{c});
            state_ = State::data;
        }
    }

    void sbPush(uint8_t c)
    {
        if (sbPayload_.size() < maxSubnegotiationSize)
        {
            sbPayload_.push_back(static_cast<char>(c));
        }
    }

    State state_ = State::data;
    uint8_t verb_ = 0;
    uint8_t sbOption_ = 0;
    std::string sbPayload_;
};

// ─── Encoder helpers ─────────────────────────────────────────────────────────

/// Double every 0xFF so the bytes survive the telnet layer unchanged.
inline std::string escapeIac(std::string_view in)
{
    std::string out;
    out.reserve(in.size());
    for (char ch : in)
    {
        out.push_back(ch);
        if (static_cast<uint8_t>(ch) == cmd::iac)
        {
            out.push_back(ch);
        }
    }
    return out;
}

/// IAC <verb> <option>
inline std::string negotiation(uint8_t verb, uint8_t option)
{
    return {static_cast<char>(cmd::iac), static_cast<char>(verb),
            static_cast<char>(option)};
}

/// IAC SB <option> <escaped payload> IAC SE
inline std::string subnegotiation(uint8_t option, std::string_view payload)
{
    std::string out{static_cast<char>(cmd::iac), static_cast<char>(cmd::sb),
                    static_cast<char>(option)};
    out += escapeIac(payload);
    out.push_back(static_cast<char>(cmd::iac));
    out.push_back(static_cast<char>(cmd::se));
    return out;
}

/// Complete NAWS subnegotiation frame for the given window size.
inline std::string nawsFrame(uint16_t width, uint16_t height)
{
    std::string payload{
        static_cast<char>(width >> 8), static_cast<char>(width & 0xFF),
        static_cast<char>(height >> 8), static_cast<char>(height & 0xFF)};
    return subnegotiation(opt::naws, payload);
}

/// Parse a NAWS payload into (width, height).
inline std::optional<std::pair<uint16_t, uint16_t>> parseNaws(
    std::string_view payload)
{
    if (payload.size() != 4)
    {
        return std::nullopt;
    }
    auto b = [&](size_t i) { return static_cast<uint8_t>(payload[i]); };
    return std::pair<uint16_t, uint16_t>{
        static_cast<uint16_t>((b(0) << 8) | b(1)),
        static_cast<uint16_t>((b(2) << 8) | b(3))};
}

/// IAC SB TERMINAL-TYPE SEND IAC SE
inline std::string terminalTypeSendFrame()
{
    return subnegotiation(opt::terminalType, std::string(1, ttypeSend));
}

/// IAC SB TERMINAL-TYPE IS <name> IAC SE
inline std::string terminalTypeIsFrame(std::string_view name)
{
    std::string payload(1, static_cast<char>(ttypeIs));
    payload += name;
    return subnegotiation(opt::terminalType, payload);
}

/// Parse a TERMINAL-TYPE payload; returns the name for an IS, nullopt else.
inline std::optional<std::string> parseTerminalTypeIs(std::string_view payload)
{
    if (payload.empty() || static_cast<uint8_t>(payload[0]) != ttypeIs)
    {
        return std::nullopt;
    }
    return std::string(payload.substr(1));
}

// ─── Negotiation policy ──────────────────────────────────────────────────────

/// Which options each side is willing to enable.
struct Policy
{
    std::bitset<256> local;  // options we agree to perform (reply WILL to DO)
    std::bitset<256> remote; // options we agree the peer performs (DO to WILL)
};

/**
 * @brief Option negotiation state machine.
 *
 * Replies are only generated when the state of an option actually changes (or
 * when an unsupported option is refused), which prevents WILL/DO loops.
 */
class Negotiator
{
  public:
    explicit Negotiator(Policy policy) : policy_(policy) {}

    /// Ask the peer / announce something ourselves. Returns bytes to send
    /// (empty if nothing needs to be sent).
    std::string request(uint8_t verb, uint8_t option)
    {
        switch (verb)
        {
            case cmd::will:
                return requestEnable(us_[option], cmd::will, option);
            case cmd::wont:
                return requestDisable(us_[option], cmd::wont, option);
            case cmd::doo:
                return requestEnable(him_[option], cmd::doo, option);
            case cmd::dont:
                return requestDisable(him_[option], cmd::dont, option);
            default:
                return {};
        }
    }

    /// Process a peer negotiation. Returns bytes to send in reply (may be
    /// empty).
    std::string handle(const Negotiation& n)
    {
        switch (n.verb)
        {
            case cmd::doo:
                return onEnable(us_[n.option], policy_.local[n.option],
                                cmd::will, cmd::wont, n.option);
            case cmd::dont:
                return onDisable(us_[n.option], cmd::wont, n.option);
            case cmd::will:
                return onEnable(him_[n.option], policy_.remote[n.option],
                                cmd::doo, cmd::dont, n.option);
            case cmd::wont:
                return onDisable(him_[n.option], cmd::dont, n.option);
            default:
                return {};
        }
    }

    /// True if we are performing @p option (peer sent DO, we sent WILL).
    bool localEnabled(uint8_t option) const
    {
        return us_[option].enabled;
    }

    /// True if the peer is performing @p option.
    bool remoteEnabled(uint8_t option) const
    {
        return him_[option].enabled;
    }

  private:
    struct OptionState
    {
        bool enabled = false;
        bool pending = false; // we asked, waiting for the answer
    };

    static std::string requestEnable(OptionState& s, uint8_t verb,
                                     uint8_t option)
    {
        if (s.enabled || s.pending)
        {
            return {};
        }
        s.pending = true;
        return negotiation(verb, option);
    }

    static std::string requestDisable(OptionState& s, uint8_t verb,
                                      uint8_t option)
    {
        if (!s.enabled && !s.pending)
        {
            return {};
        }
        s.enabled = false;
        s.pending = false;
        return negotiation(verb, option);
    }

    static std::string onEnable(OptionState& s, bool accepted,
                                uint8_t acceptVerb, uint8_t refuseVerb,
                                uint8_t option)
    {
        if (s.pending)
        {
            // Answer to our own request.
            s.pending = false;
            s.enabled = true;
            return {};
        }
        if (!accepted)
        {
            return negotiation(refuseVerb, option);
        }
        if (s.enabled)
        {
            return {};
        }
        s.enabled = true;
        return negotiation(acceptVerb, option);
    }

    static std::string onDisable(OptionState& s, uint8_t verb, uint8_t option)
    {
        if (s.enabled)
        {
            s.enabled = false;
            return negotiation(verb, option);
        }
        s.pending = false; // peer refused our request; no answer to a refusal
        return {};
    }

    Policy policy_;
    std::array<OptionState, 256> us_{};
    std::array<OptionState, 256> him_{};
};

} // namespace NSNAME::telnet
