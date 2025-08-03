#include "spdm_responder.hpp"

#include "command_line_parser.hpp"
int main(int argc, const char* argv[])
{
    auto [port] = getArgs(parseCommandline(argc, argv), "--port,-p");
    reactor::getLogger().setLogLevel(reactor::LogLevel::DEBUG);
    boost::asio::io_context io_context;
    if (!port.has_value())
    {
        LOG_ERROR("Port number cannot be empty");
        return 1;
    }
    SpdmTcpServer server(io_context, std::stoi(port.value().data()));

    SpdmResponder resonder(server);
    io_context.post([&resonder]() { resonder.run(); });
    io_context.run();
    return 0;
}
