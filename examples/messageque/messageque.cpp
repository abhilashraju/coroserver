

#include "messageque.hpp"

#include "command_line_parser.hpp"
#include "logger.hpp"

int main(int argc, const char* argv[])
{
    net::io_context io_context;
    ssl::context ssl_context(ssl::context::sslv23_client);
    MessageQueue messageQueue(io_context.get_executor(), ssl_context,
                              "127.0.0.1", "8080");
    unsigned i = 0;
    while (i++ < 10)
    {
        messageQueue.addMessage([](auto streamer) -> net::awaitable<void> {
            std::string header = "Hello, server!\r\n";
            co_await streamer.write(net::buffer(header));
            std::array<char, 1024> data{0};
            while (true)
            {
                auto [ec, bytes] = co_await streamer.read(net::buffer(data));
                if (ec)
                {
                    std::cerr << "Receive error: " << ec.message() << std::endl;
                    streamer.close();
                    co_return;
                }

                std::cout << "Received: " << data.data() << std::endl;
                std::fill(data.begin(), data.end(), 0);
            }
        });
    }

    io_context.run();
    return 0;
}
