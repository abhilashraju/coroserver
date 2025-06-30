

#include "tcp_client.hpp"

#include "command_line_parser.hpp"
#include "logger.hpp"

int main(int argc, const char* argv[])
{
    auto [ip, port, data] =
        getArgs(parseCommandline(argc, argv), "--ipaddress,-ip", "--port,-p",
                "--data,-d");
    if (!ip || !port)
    {
        std::cerr << "Usage: tcp_client -ip <IP> -p <PORT> "
                     "-d <DATA>";
        return 1;
    }
    net::io_context io_context;

    net::co_spawn(
        io_context,
        [&io_context, ip = std::string(ip.value()),
         port = std::string(port.value()),
         data = std::string(data.value())]() -> net::awaitable<void> {
            ssl::context ssl_context(ssl::context::sslv23_client);
            TcpClient client(io_context.get_executor(), ssl_context);
            auto ec = co_await client.connect(ip, port);
            if (ec)
            {
                std::cerr << "Connect error: " << ec.message() << std::endl;
                co_return;
            }
            std::string message = !data.empty() ? data : "Hello, Server!";
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
