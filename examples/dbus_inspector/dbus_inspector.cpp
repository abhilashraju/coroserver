
#include "dbus_handlers.hpp"

#include <iostream>
#include <optional>
#include <string>
int main()
{
    try
    {
        boost::asio::io_context io_context;

        auto conn = std::make_shared<sdbusplus::asio::connection>(io_context);

        boost::asio::ssl::context ssl_context(
            boost::asio::ssl::context::sslv23);

        // Load server certificate and private key
        ssl_context.set_options(boost::asio::ssl::context::default_workarounds |
                                boost::asio::ssl::context::no_sslv2 |
                                boost::asio::ssl::context::single_dh_use);
        std::cerr << "Cert Loading: \n";
        ssl_context.use_certificate_chain_file(
            "/etc/ssl/private/server-cert.pem");
        ssl_context.use_private_key_file("/etc/ssl/private/server-key.pem",
                                         boost::asio::ssl::context::pem);
        std::cerr << "Cert Loaded: \n";
        HttpRouter router;
        router.setIoContext(io_context);
        TcpStreamType acceptor(io_context, 8080, ssl_context);
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
