
#include "command_line_parser.hpp"
#include "event_listener.hpp"

#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_set>

int main(int argc, const char* argv[])
{
    try
    {
        getLogger().setLogLevel(LogLevel::DEBUG);
        LOG_INFO("Starting Redfish listener");
        auto [cert, p, ip] = getArgs(parseCommandline(argc, argv), "--cert,-c",
                                     "--port,-p", "--ip,-ip");

        boost::asio::io_context io_context;

        boost::asio::ssl::context ssl_context(
            boost::asio::ssl::context::sslv23);

        // Load server certificate and private key
        ssl_context.set_options(boost::asio::ssl::context::default_workarounds |
                                boost::asio::ssl::context::no_sslv2 |
                                boost::asio::ssl::context::single_dh_use);
        LOG_DEBUG("Loading SSL certificate and key");
        std::string certDir = "/etc/ssl/private";
        if (cert)
        {
            certDir = std::string(*cert);
        }
        ssl_context.use_certificate_chain_file(certDir + "/server-cert.pem");
        ssl_context.use_private_key_file(certDir + "/server-key.pem",
                                         boost::asio::ssl::context::pem);
        std::string port(p.value_or("8080"));
        std::string ipAddress(ip.value_or("0.0.0.0"));
        HttpRouter router;
        router.setIoContext(io_context);
        TcpStreamType acceptor(io_context.get_executor(), ipAddress,
                               std::atoi(port.data()), ssl_context);
        HttpServer server(io_context, acceptor, router);
        EventListener eventListener(router, io_context, ipAddress, port);

        io_context.run();
    }
    catch (std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return 1;
    }

    return 0;
}
