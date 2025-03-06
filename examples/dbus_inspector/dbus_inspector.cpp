
#include "command_line_parser.hpp"
#include "dbus_handlers.hpp"

#include <iostream>
#include <optional>
#include <string>
int main(int argc, const char* argv[])
{
    try
    {
        reactor::getLogger().setLogLevel(reactor::LogLevel::DEBUG);
        auto [cert] = getArgs(parseCommandline(argc, argv), "--cert,-c");

        boost::asio::io_context io_context;

        auto conn = std::make_shared<sdbusplus::asio::connection>(io_context);

        boost::asio::ssl::context ssl_context(
            boost::asio::ssl::context::sslv23);

        // Load server certificate and private key
        ssl_context.set_options(boost::asio::ssl::context::default_workarounds |
                                boost::asio::ssl::context::no_sslv2 |
                                boost::asio::ssl::context::single_dh_use);
        std::cerr << "Cert Loading: \n";
        std::string certDir = "/etc/ssl/private";
        if (cert)
        {
            certDir = std::string(*cert);
        }

        ssl_context.use_certificate_chain_file(certDir + "/server-cert.pem");
        ssl_context.use_private_key_file(certDir + "/server-key.pem",
                                         boost::asio::ssl::context::pem);
        std::cerr << "Cert Loaded: \n";
        HttpRouter router;
        router.setIoContext(io_context);
        TcpStreamType acceptor(io_context.get_executor(), 8080, ssl_context);
        HttpServer server(io_context, acceptor, router);
        DbusHandlers dbusHandlers(*conn, router);
        io_context.run();
    }

    catch (std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return 1;
    }

    return 0;
}
