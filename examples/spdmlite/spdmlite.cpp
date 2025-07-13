

#include "certificate_exchange.hpp"
#include "command_line_parser.hpp"
#include "eventmethods.hpp"
#include "eventqueue.hpp"
#include "logger.hpp"
#include "pic_controller.hpp"
#include "sdbus_calls.hpp"
#include "spdm_handshake.hpp"

#include <nlohmann/json.hpp>

#include <csignal>
constexpr auto IP_EVENT = "IPEvent";
void signalHandler(int signal)
{
    if (signal == SIGTERM || signal == SIGINT)
    {
        LOG_INFO("Termination signal received, storing event queue...");
        // if (peventQueue)
        // {
        //     peventQueue->store();
        // }
        exit(0);
    }
}

void setupSignalHandlers()
{
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGINT, signalHandler);
}
net::awaitable<boost::system::error_code> publisher(
    EventQueue& eventQue, Streamer streamer, const std::string& event)
{
    LOG_DEBUG("Received Event for publish: {}", event);
    auto [id, data] = parseEvent(event);
    eventQue.addEvent(makeEvent(data));
    co_return boost::system::error_code{};
}

int main(int argc, const char* argv[])
{
    auto [conf] = getArgs(parseCommandline(argc, argv), "--conf,-c");
    if (!conf)
    {
        LOG_ERROR(
            "No config file provided :eg event_broker --conf /path/to/conf");

        return 1;
    }
    try
    {
        auto json = nlohmann::json::parse(std::ifstream(conf.value().data()));

        auto cert = json.value("cert", std::string{});
        auto privkey = json.value("privkey", std::string{});
        auto pubkey = json.value("pubkey", std::string{});
        auto port = json.value("port", std::string{});
        auto myip = json.value("ip", std::string{"0.0.0.0"});
        auto rip = json.value("remote_ip", std::string{});
        auto rp = json.value("remote_port", std::string{});
        std::vector<std::string> resources =
            json.value("resources", std::vector<std::string>{});
        auto maxConnections = 1;

        reactor::Logger<std::ostream>& logger = reactor::getLogger();
        logger.setLogLevel(reactor::LogLevel::DEBUG);
        net::io_context io_context;
        ssl::context ssl_server_context(ssl::context::sslv23_server);

        // Load server certificate and private key
        ssl_server_context.set_options(
            boost::asio::ssl::context::default_workarounds |
            boost::asio::ssl::context::no_sslv2 |
            boost::asio::ssl::context::single_dh_use);
        if (!cert.empty())
        {
            ssl_server_context.use_certificate_chain_file(cert);
            ssl_server_context.use_private_key_file(
                privkey, boost::asio::ssl::context::pem);
        }
        ssl::context ssl_client_context(ssl::context::sslv23_client);
        TcpStreamType acceptor(io_context.get_executor(), myip,
                               std::atoi(port.data()), ssl_server_context);
        EventQueue eventQueue(io_context.get_executor(), acceptor,
                              ssl_client_context, maxConnections);
        auto conn = std::make_shared<sdbusplus::asio::connection>(io_context);

        if (!rip.empty() && !rp.empty())
        {
            eventQueue.setQueEndPoint(rip, rp);
        }
        eventQueue.addEventConsumer(
            "Publish", std::bind_front(publisher, std::ref(eventQueue)));
        // eventQueue.load();
        setupSignalHandlers();
        SpdmHandler spdmHandler(privkey, pubkey, eventQueue, io_context);
        spdmHandler.setSpdmFinishHandler(
            [&eventQueue, conn](bool status) -> net::awaitable<void> {
                LOG_INFO("SPDM Handshake finished with status: {}", status);
                co_await setProperty(
                    *conn, PicController::busName, PicController::objPath,
                    PicController::interface, "ProvisioningState", status);
                co_return;
            });
        for (const auto& resource : resources)
        {
            spdmHandler.addToMeasure(resource);
        }

        PicController picController(conn, [&eventQueue](bool state) {
            LOG_INFO("Provisioning state changed: {}", state);
            if (state)
            {
                eventQueue.addEvent(makeEvent(SPDM_START, ""));
            }
        });
        conn->request_name(PicController::busName);
        io_context.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
    }
    return 0;
}
