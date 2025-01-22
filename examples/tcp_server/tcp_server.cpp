

#include "tcp_server.hpp"

#include "command_line_parser.hpp"
#include "logger.hpp"

int main()
{
    net::io_context io_context;
    ssl::context ssl_context(ssl::context::sslv23_server);

    // Load server certificate and private key
    ssl_context.set_options(boost::asio::ssl::context::default_workarounds |
                            boost::asio::ssl::context::no_sslv2 |
                            boost::asio::ssl::context::single_dh_use);
    ssl_context.use_certificate_chain_file("/tmp/server-cert.pem");
    ssl_context.use_private_key_file("/tmp/server-key.pem",
                                     boost::asio::ssl::context::pem);
    TcpStreamType acceptor(io_context, 8080, ssl_context);
    auto router = [](auto reader, auto writer) -> net::awaitable<void> {
        while (true)
        {
            std::array<char, 1024> data;
            boost::system::error_code ec;
            size_t bytes{0};
            std::tie(ec, bytes) = co_await reader.read(net::buffer(data));
            if (ec)
            {
                LOG_ERROR("Error reading: {}", ec.message());
                co_return;
            }
            LOG_INFO("Received: {}", std::string(data.data(), bytes));
            std::string response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: 13\r\n"
                "Content-Type: text/plain\r\n"
                "\r\n"
                "Hello, World!";
            std::tie(ec, bytes) = co_await writer.write(net::buffer(response));
            if (ec)
            {
                LOG_ERROR("Error writing: {}", ec.message());
                co_return;
            }
        }
    };
    TcpServer server(io_context, acceptor, router);
    io_context.run();
}
