

#include "certificate_exchange.hpp"
#include "command_line_parser.hpp"
#include "eventmethods.hpp"
#include "eventqueue.hpp"
#include "logger.hpp"
#include "measurements.hpp"

#include <nlohmann/json.hpp>

#include <csignal>
EventQueue* peventQueue{nullptr};
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
bool checkMeasurementResult(const MeasurementHandler::MeasurementResult& result)
{
    std::vector<std::string> failedExecutables =
        std::ranges::views::filter(
            result, [](const auto& pair) { return !pair.second; }) |
        std::ranges::views::keys | std::ranges::to<std::vector<std::string>>();
    if (failedExecutables.empty())
    {
        LOG_INFO("All measurements passed successfully.");
        return true;
    }

    LOG_ERROR("Failed measurements for executables:");
    for (const auto& exe : failedExecutables)
    {
        LOG_ERROR(" - {}", exe);
    }

    return false;
}
net::awaitable<boost::system::error_code> ipEventConsumer(
    EventQueue* eventQueue, Streamer streamer, const std::string& event)

{
    LOG_DEBUG("Received event: {}", event);
    auto [id, body] = parseEvent(event);
    if (id == IP_EVENT)
    {
        auto jsonBody = nlohmann::json::parse(body);
        auto ip = jsonBody.value("ip", std::string{});
        if (ip.empty())
        {
            LOG_ERROR("No IP address provided in the event body.");
            co_return boost::asio::error::invalid_argument;
        }
        auto port = jsonBody.value("port", std::string{});
        auto ep = eventQueue->getLocalEndpoint();
        auto myIp = ep.address().to_string();
        auto myport = std::to_string(ep.port());
        if (port.empty())
        {
            port = myport;
        }

        if (eventQueue->getQueEndPoint() != EventQueue::EndPoint{ip, port})
        {
            // set the remote endpoint in the event queue
            eventQueue->setQueEndPoint(ip, port);
            // send my endpoint to remote side
            nlohmann::json ipEvent;
            ipEvent["ip"] = myIp;
            ipEvent["port"] = myport;
            eventQueue->addEvent(makeEvent(IP_EVENT, ipEvent.dump()));
            LOG_INFO("IP address set to: {}, port: {}", ip, port);
        }

        co_return boost::system::error_code{};
    }

    co_return boost::system::error_code{};
}
void afterMeasurements(CertificateExchanger& exchanger,
                       MeasurementHandler::MeasurementResult result)
{
    LOG_DEBUG("All measurements received, results:");
    if (checkMeasurementResult(result))
    {
        LOG_INFO("All measurements passed successfully. Sending certificates.");
        exchanger.sendCertificates();
    }
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
        peventQueue = &eventQueue;
        eventQueue.addEventConsumer(
            IP_EVENT, std::bind_front(ipEventConsumer, &eventQueue));
        MeasurementHandler measurementHandler(privkey, pubkey, eventQueue,
                                              io_context);
        for (const auto& resource : resources)
        {
            measurementHandler.addToMeasure(resource);
        }
        // eventQueue.load();
        setupSignalHandlers();
        measurementHandler.sendMyMeasurement();

        auto pkeyptr = loadPrvateKey(privkey);
        auto caname = generateCAName("BMC CA");
        CertificateExchanger exchanger(pkeyptr.get(),
                                       caname.get(), // Create a new X509_NAME
                                       eventQueue, io_context);
        measurementHandler.waitForRemoteMeasurements(
            std::bind_front(&afterMeasurements, std::ref(exchanger)));

        io_context.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
    }
    return 0;
}
