

#include "command_line_parser.hpp"
#include "logger.hpp"
#include "taskqueue.hpp"

int main(int argc, const char* argv[])
{
    net::io_context io_context;
    ssl::context ssl_context(ssl::context::sslv23_client);
    TaskQueue messageQueue(io_context.get_executor(), ssl_context, "127.0.0.1",
                           "8080");
    unsigned i = 0;
    while (i++ < 10)
    {
        messageQueue.addTask(
            [](auto streamer) -> net::awaitable<boost::system::error_code> {
                std::string header = "Hello, server!\r\n";
                co_await streamer.write(net::buffer(header));
                std::array<char, 1024> data{0};

                auto [ec, bytes] = co_await streamer.read(net::buffer(data));
                if (ec)
                {
                    LOG_ERROR("Receive error: {}", ec.message());
                    co_return ec;
                }

                std::cout << "Received: " << data.data() << std::endl;
                co_return boost::system::error_code{};
            });
    }

    io_context.run();
    return 0;
}
