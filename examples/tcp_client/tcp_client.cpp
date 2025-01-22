

#include "tcp_client.hpp"

#include "command_line_parser.hpp"
#include "logger.hpp"

int main(int argc, const char* argv[])
{
    net::io_context io_context;

    net::co_spawn(
        io_context,
        [&io_context]() -> net::awaitable<void> {
            ssl::context ssl_context(ssl::context::sslv23_client);
            TcpClient client(io_context, ssl_context);
            auto ec = co_await client.connect("127.0.0.1", "8080");
            if (ec)
            {
                std::cerr << "Connect error: " << ec.message() << std::endl;
                co_return;
            }
            std::string message = "Hello, server!";
            size_t bytes{0};
            std::tie(ec, bytes) = co_await client.write(net::buffer(message));
            if (ec)
            {
                std::cerr << "Send error: " << ec.message() << std::endl;
                co_return;
            }
            std::array<char, 1024> data{0};
            while (true)
            {
                auto [recv_ec, bytes] = co_await client.read(net::buffer(data));
                if (recv_ec)
                {
                    std::cerr
                        << "Receive error: " << recv_ec.message() << std::endl;
                    co_return;
                }

                std::cout << "Received: " << data.data() << std::endl;
                std::fill(data.begin(), data.end(), 0);
            }
        },
        net::detached);

    io_context.run();
    return 0;
}
