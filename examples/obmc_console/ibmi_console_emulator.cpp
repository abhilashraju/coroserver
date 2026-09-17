/**
 * @file ibmi_console_emulator.cpp
 * @brief Standalone Unix-socket wrapper for the shared IBM i emulator.
 */

#include "ibmi_emulator.hpp"

#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

using namespace NSNAME;
using UnixProtocol = boost::asio::local::stream_protocol;

namespace
{

constexpr std::string_view defaultSocketPath =
    "/tmp/ibmi-console-emulator.sock";

bool removeExistingSocket(const std::string& socketPath)
{
    struct stat status{};
    if (lstat(socketPath.c_str(), &status) != 0)
    {
        return errno == ENOENT;
    }

    if (!S_ISSOCK(status.st_mode))
    {
        std::cerr << "Refusing to replace non-socket path: " << socketPath
                  << '\n';
        return false;
    }

    if (unlink(socketPath.c_str()) != 0)
    {
        std::cerr << "Unable to remove stale socket " << socketPath << ": "
                  << std::strerror(errno) << '\n';
        return false;
    }

    return true;
}

void send(UnixProtocol::socket& client, const std::vector<uint8_t>& frame)
{
    boost::asio::write(client, boost::asio::buffer(frame));
}

void serveClient(UnixProtocol::socket& client)
{
    IbmiEmulator emulator;
    send(client, emulator.initialDisplay());

    std::array<uint8_t, 256> buffer{};
    boost::system::error_code error;
    while (!emulator.isClosed())
    {
        const size_t bytesRead =
            client.read_some(boost::asio::buffer(buffer), error);
        if (error)
        {
            return;
        }

        send(client, emulator.process(
                         std::span<const uint8_t>(buffer.data(), bytesRead)));
    }
}

} // namespace

int main(int argc, char* argv[])
{
    const std::string socketPath =
        argc > 1 ? argv[1] : std::string(defaultSocketPath);
    if (socketPath.empty() ||
        socketPath.size() >= sizeof(sockaddr_un::sun_path))
    {
        std::cerr << "Socket path must be between 1 and "
                  << sizeof(sockaddr_un::sun_path) - 1 << " bytes.\n";
        return EXIT_FAILURE;
    }

    if (!removeExistingSocket(socketPath))
    {
        return EXIT_FAILURE;
    }

    try
    {
        boost::asio::io_context ioContext;
        UnixProtocol::acceptor acceptor(ioContext,
                                        UnixProtocol::endpoint(socketPath));
        std::cout << "Dummy IBM i 5250 emulator listening on " << socketPath
                  << '\n';

        while (true)
        {
            UnixProtocol::socket client(ioContext);
            acceptor.accept(client);
            serveClient(client);
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "IBM i console emulator failed: " << error.what() << '\n';
        unlink(socketPath.c_str());
        return EXIT_FAILURE;
    }
}
