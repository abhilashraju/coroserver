#include "command_line_parser.hpp"
#include "eventmethods.hpp"
#include "logger.hpp"
#include "tcp_client.hpp"
#include "utilities.hpp"

#include <nlohmann/json.hpp>
int main(int argc, const char* argv[])
{
    auto [id, data, conf] = getArgs(parseCommandline(argc, argv), "--id,-i",
                                    "--data,-d", "--conf,-c");
    if (!id && !conf)
    {
        LOG_ERROR("No event id or data provided");
        LOG_ERROR("eg event_broker --id <event id> --data <event data> or "
                  "--conf /path/to/conf");
        return 1;
    }
    net::io_context io_context;
    std::string eventID;
    std::string eventData;
    std::string eventType = "Publish";
    std::string ip = "127.0.0.1";
    std::string port = "8080";
    if (id)
    {
        eventID = *id;
        eventData = data ? *data : "*";
    }
    else
    {
        auto json = nlohmann::json::parse(std::ifstream(conf.value().data()));
        eventID = json.value("id", std::string{});
        eventData = json.value("data", std::string{});
        eventType = json.value("type", std::string{"Publish"});
        ip = json.value("ip", std::string{"127.0.0.1"});
        port = json.value("port", std::string{"8080"});
    }
    net::co_spawn(
        io_context,
        [&]() -> net::awaitable<void> {
            ssl::context ssl_context(ssl::context::sslv23_client);
            TcpClient client(io_context.get_executor(), ssl_context);
            auto ec = co_await client.connect(ip, port);
            if (ec)
            {
                LOG_ERROR("Connect error: {}", ec.message());
                co_return;
            }
            std::string message = [&](auto type) {
                if (type == "Publish")
                {
                    return makeEvent("Publish",
                                     makeEvent(eventID, eventData, ""));
                }
                return makeEvent(eventID, eventData);
            }(eventType);

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
