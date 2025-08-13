#include "mctp_requester.hpp"

#include "logger.hpp"
net::awaitable<void> makeRequest(boost::asio::io_context& ctx)
{
    // Create an instance of MCTPRequester
    MCTPRequester requester(ctx);

    // Example usage of the MCTPRequester
    uint8_t eid = 0x01;                                // Example EID
    std::vector<uint8_t> message = {0x01, 0x02, 0x03}; // Example message

    auto ec = co_await requester.sendMessage(eid, message);
    if (ec)
    {
        LOG_ERROR("Error sending message: {}", ec.message());
        co_return;
    }

    ec = co_await requester.receiveMessage(std::span<uint8_t>(message));
    if (!ec)
    {
        LOG_DEBUG("Received message: {}",
                  std::string(message.begin(), message.end()));
    }
    else
    {
        LOG_ERROR("Error receiving message: {}", ec.message());
    }
}
int main()
{
    boost::asio::io_context ioContext;
    net::co_spawn(ioContext, std::bind_front(makeRequest, std::ref(ioContext)),
                  net::detached);

    // Run the io_context to process the asynchronous operations
    ioContext.run();

    return 0;
}
