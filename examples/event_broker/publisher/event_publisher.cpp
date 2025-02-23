#include "command_line_parser.hpp"
#include "eventmethods.hpp"
#include "logger.hpp"
#include "tcp_client.hpp"
#include "utilities.hpp"
int main(int argc, const char* argv[])
{
    auto [id,
          data] = getArgs(parseCommandline(argc, argv), "--id,-i", "--data,-d");
    if (!id)
    {
        LOG_ERROR("No event id or data provided");
        LOG_ERROR("eg event_broker --id <event id> --data <event data>");
        return 1;
    }
    net::io_context io_context;
    std::string eventID(*id);
    std::string eventData(data ? *data : "*");
    net::co_spawn(
        io_context,
        [&io_context, eventID, eventData]() -> net::awaitable<void> {
            ssl::context ssl_context(ssl::context::sslv23_client);
            TcpClient client(io_context.get_executor(), ssl_context);
            auto ec = co_await client.connect("127.0.0.1", "8080");
            if (ec)
            {
                LOG_ERROR("Connect error: {}", ec.message());
                co_return;
            }
            std::string message =
                makeEvent("Publish", makeEvent(eventID, eventData, ""));
            size_t bytes{0};
            std::tie(ec, bytes) = co_await client.write(net::buffer(message));
            if (ec)
            {
                LOG_ERROR("Send error: {}", ec.message());
                co_return;
            }
            std::tie(ec, bytes) = co_await client.read(net::buffer(message));
            if (ec)
            {
                LOG_ERROR("Read error: {}", ec.message());
                co_return;
            }
            LOG_INFO("Recieved: {}", message);
        },
        net::detached);

    io_context.run();
    return 0;
}
