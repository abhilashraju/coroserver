/**
 * @file test_telnet_protocol.cpp
 * @brief Unit tests for include/telnet_protocol.hpp (no sockets required).
 */
#define BOOST_TEST_MODULE TelnetProtocolTests
#include "telnet_protocol.hpp"

#include <boost/test/included/unit_test.hpp>

using namespace NSNAME::telnet;

namespace
{
struct Result
{
    std::string data;
    std::vector<Event> events;
};

Result decodeAll(const std::string& bytes, size_t chunk)
{
    Decoder d;
    Result r;
    for (size_t i = 0; i < bytes.size(); i += chunk)
    {
        d.feed(std::string_view(bytes).substr(i, chunk), r.data, r.events);
    }
    return r;
}

std::string bytes(std::initializer_list<int> v)
{
    std::string s;
    for (int b : v)
    {
        s.push_back(static_cast<char>(b));
    }
    return s;
}
} // namespace

BOOST_AUTO_TEST_CASE(plain_data_passthrough)
{
    auto r = decodeAll("hello", 64);
    BOOST_CHECK_EQUAL(r.data, "hello");
    BOOST_CHECK(r.events.empty());
}

BOOST_AUTO_TEST_CASE(escaped_iac_is_single_0xff)
{
    auto in = bytes({'a', 255, 255, 'b'});
    for (size_t chunk : {1, 2, 3, 64})
    {
        auto r = decodeAll(in, chunk);
        BOOST_CHECK_EQUAL(r.data, bytes({'a', 255, 'b'}));
        BOOST_CHECK(r.events.empty());
    }
}

BOOST_AUTO_TEST_CASE(negotiation_split_across_reads)
{
    auto in = bytes({255, cmd::will, opt::echo, 'x'});
    for (size_t chunk : {1, 2, 3, 64})
    {
        auto r = decodeAll(in, chunk);
        BOOST_CHECK_EQUAL(r.data, "x");
        BOOST_REQUIRE_EQUAL(r.events.size(), 1u);
        auto* n = std::get_if<Negotiation>(&r.events[0]);
        BOOST_REQUIRE(n != nullptr);
        BOOST_CHECK_EQUAL(n->verb, cmd::will);
        BOOST_CHECK_EQUAL(n->option, opt::echo);
    }
}

BOOST_AUTO_TEST_CASE(naws_subnegotiation_roundtrip)
{
    auto frame = nawsFrame(132, 43);
    for (size_t chunk : {1, 2, 5, 64})
    {
        auto r = decodeAll(frame, chunk);
        BOOST_CHECK(r.data.empty());
        BOOST_REQUIRE_EQUAL(r.events.size(), 1u);
        auto* sb = std::get_if<Subnegotiation>(&r.events[0]);
        BOOST_REQUIRE(sb != nullptr);
        BOOST_CHECK_EQUAL(sb->option, opt::naws);
        auto size = parseNaws(sb->payload);
        BOOST_REQUIRE(size.has_value());
        BOOST_CHECK_EQUAL(size->first, 132);
        BOOST_CHECK_EQUAL(size->second, 43);
    }
}

BOOST_AUTO_TEST_CASE(naws_with_0xff_dimension_is_escaped)
{
    // width 255 -> payload byte 0xFF must be doubled on the wire.
    auto frame = nawsFrame(255, 24);
    auto r = decodeAll(frame, 1);
    BOOST_REQUIRE_EQUAL(r.events.size(), 1u);
    auto* sb = std::get_if<Subnegotiation>(&r.events[0]);
    BOOST_REQUIRE(sb != nullptr);
    auto size = parseNaws(sb->payload);
    BOOST_REQUIRE(size.has_value());
    BOOST_CHECK_EQUAL(size->first, 255);
    BOOST_CHECK_EQUAL(size->second, 24);
}

BOOST_AUTO_TEST_CASE(naws_rejects_bad_length)
{
    BOOST_CHECK(!parseNaws("abc").has_value());
}

BOOST_AUTO_TEST_CASE(terminal_type_frames)
{
    auto r = decodeAll(terminalTypeIsFrame("xterm-256color"), 3);
    BOOST_REQUIRE_EQUAL(r.events.size(), 1u);
    auto* sb = std::get_if<Subnegotiation>(&r.events[0]);
    BOOST_REQUIRE(sb != nullptr);
    BOOST_CHECK_EQUAL(sb->option, opt::terminalType);
    BOOST_CHECK_EQUAL(parseTerminalTypeIs(sb->payload).value_or(""),
                      "xterm-256color");

    auto s = decodeAll(terminalTypeSendFrame(), 64);
    auto* ssb = std::get_if<Subnegotiation>(&s.events.at(0));
    BOOST_REQUIRE(ssb != nullptr);
    BOOST_CHECK(!parseTerminalTypeIs(ssb->payload).has_value());
}

BOOST_AUTO_TEST_CASE(cr_nul_is_collapsed_to_cr)
{
    auto r = decodeAll(bytes({'a', '\r', 0, 'b', '\r', '\n'}), 2);
    BOOST_CHECK_EQUAL(r.data, "a\rb\r\n");
}

BOOST_AUTO_TEST_CASE(other_iac_commands_are_events)
{
    auto r = decodeAll(bytes({255, cmd::nop, 'z'}), 1);
    BOOST_CHECK_EQUAL(r.data, "z");
    BOOST_REQUIRE_EQUAL(r.events.size(), 1u);
    auto* c = std::get_if<Command>(&r.events[0]);
    BOOST_REQUIRE(c != nullptr);
    BOOST_CHECK_EQUAL(c->code, cmd::nop);
}

BOOST_AUTO_TEST_CASE(escape_iac_encoder)
{
    BOOST_CHECK_EQUAL(escapeIac(bytes({'a', 255, 'b'})),
                      bytes({'a', 255, 255, 'b'}));
}

BOOST_AUTO_TEST_CASE(oversized_subnegotiation_is_truncated)
{
    std::string payload(maxSubnegotiationSize + 100, 'x');
    auto r = decodeAll(subnegotiation(opt::terminalType, payload), 64);
    BOOST_REQUIRE_EQUAL(r.events.size(), 1u);
    auto* sb = std::get_if<Subnegotiation>(&r.events[0]);
    BOOST_REQUIRE(sb != nullptr);
    BOOST_CHECK_EQUAL(sb->payload.size(), maxSubnegotiationSize);
}

BOOST_AUTO_TEST_CASE(negotiator_accepts_and_refuses_per_policy)
{
    Policy p;
    p.local.set(opt::echo);
    p.remote.set(opt::naws);
    Negotiator n(p);

    BOOST_CHECK_EQUAL(n.handle({cmd::doo, opt::echo}),
                      negotiation(cmd::will, opt::echo));
    BOOST_CHECK(n.localEnabled(opt::echo));
    // Repeat must not produce another reply (no loops).
    BOOST_CHECK(n.handle({cmd::doo, opt::echo}).empty());

    BOOST_CHECK_EQUAL(n.handle({cmd::will, opt::naws}),
                      negotiation(cmd::doo, opt::naws));
    BOOST_CHECK(n.remoteEnabled(opt::naws));

    // Unsupported options are refused.
    BOOST_CHECK_EQUAL(n.handle({cmd::doo, 99}), negotiation(cmd::wont, 99));
    BOOST_CHECK_EQUAL(n.handle({cmd::will, 99}), negotiation(cmd::dont, 99));
}

BOOST_AUTO_TEST_CASE(negotiator_own_request_answer_is_not_echoed)
{
    Policy p;
    p.local.set(opt::echo);
    p.remote.set(opt::naws);
    Negotiator n(p);

    BOOST_CHECK_EQUAL(n.request(cmd::will, opt::echo),
                      negotiation(cmd::will, opt::echo));
    BOOST_CHECK(n.request(cmd::will, opt::echo).empty()); // already pending
    BOOST_CHECK(n.handle({cmd::doo, opt::echo}).empty()); // the answer
    BOOST_CHECK(n.localEnabled(opt::echo));

    BOOST_CHECK_EQUAL(n.request(cmd::doo, opt::naws),
                      negotiation(cmd::doo, opt::naws));
    BOOST_CHECK(n.handle({cmd::will, opt::naws}).empty());
    BOOST_CHECK(n.remoteEnabled(opt::naws));
}

BOOST_AUTO_TEST_CASE(negotiator_refusal_of_our_request_is_silent)
{
    Policy p;
    Negotiator n(p);
    BOOST_CHECK_EQUAL(n.request(cmd::doo, opt::terminalType),
                      negotiation(cmd::doo, opt::terminalType));
    BOOST_CHECK(n.handle({cmd::wont, opt::terminalType}).empty());
    BOOST_CHECK(!n.remoteEnabled(opt::terminalType));
}
