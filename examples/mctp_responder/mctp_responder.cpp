
#include "mctp_responder.hpp"

#include "logger.hpp"
using namespace NSNAME;
net::awaitable<void> makeResponder(boost::asio::io_context& ctx,
                                   uint8_t localEid)
{
    // Create an instance of MCTPResponder
    MCTPResponder responder(ctx, localEid);

    // Buffer to hold received messages
    std::vector<uint8_t> buffer(MCTPResponder::maxMessageSize);

    while (true)
    {
        auto [ec, bytesReceived] = co_await responder.receiveMessage(buffer);
        if (ec)
        {
            LOG_ERROR("Error receiving message: {}", ec.message());
            continue;
        }

        LOG_DEBUG("Received {} bytes from EID: {}", bytesReceived, localEid);
        // Process the received message here
        // For example, you can send a response back
    }
}
int main()
{
    boost::asio::io_context ioContext;
    uint8_t localEid = 0x01; // Example local EID for the responder

    net::co_spawn(ioContext,
                  std::bind_front(makeResponder, std::ref(ioContext), localEid),
                  net::detached);

    // Run the io_context to process the asynchronous operations
    ioContext.run();

    return 0;
}
