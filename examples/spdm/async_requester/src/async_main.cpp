/**
 * async_main.cpp — Async SPDM requester entry point.
 *
 * Demonstrates fully asynchronous SPDM requester operation:
 *  - connect + GET_VERSION + GET_CAPABILITIES + NEGOTIATE_ALGORITHMS all
 *    driven via co_await — the io_context is never blocked.
 *  - Multiple requester coroutines can be co_spawn'd concurrently if needed.
 *
 * Usage: async_spdm_requester --port 2323 [--host 127.0.0.1]
 */

#include "async_spdm_requester.hpp"
#include "command_line_parser.hpp"
#include "spdm_io_redirect.hpp"
#include "worker.hpp"

#include <boost/asio.hpp>

#include <format>
#include <string>

using namespace reactor;
namespace net = boost::asio;

int main(int argc, const char* argv[])
{
    auto [port, host] = reactor::getArgs(
        reactor::parseCommandline(argc, argv), "--port,-p", "--host,-H");
    reactor::getLogger().setLogLevel(reactor::LogLevel::DEBUG);

    int iport = 2323;
    if (port)
    {
        iport = std::atoi(port.value().data());
    }
    std::string hostAddr = "127.0.0.1";
    if (host)
    {
        hostAddr = std::string(host.value());
    }

    net::io_context io;

    // Redirect libspdm debug output to logger
    spdm_io_redirect::enableSpdmLogging(io);

    // Spawn the async requester coroutine
    net::co_spawn(
        io,
        [&io, hostAddr, iport]() -> net::awaitable<void> {
            AsyncSpdmRequester requester(io);
            co_await requester.run(hostAddr,
                                   static_cast<uint16_t>(iport));
        },
        net::detached);

    LOG_INFO("Async SPDM requester connecting to {}:{}", hostAddr, iport);
    io.run();
    return 0;
}
