

#include "certificate_exchange.hpp"
#include "command_line_parser.hpp"
#include "eventmethods.hpp"
#include "eventqueue.hpp"
#include "logger.hpp"
#include "measurements.hpp"

#include <nlohmann/json.hpp>

#include <csignal>
EventQueue* peventQueue{nullptr};
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

        auto dest = json.value("remote", std::string{});
        auto cert = json.value("cert", std::string{});
        auto privkey = json.value("privkey", std::string{});
        auto pubkey = json.value("pubkey", std::string{});
        auto rp = json.value("remote-port", std::string{});
        auto port = json.value("port", std::string{});
        std::vector<std::string> resources =
            json.value("resources", std::vector<std::string>{});
        auto maxConnections = 1;

        reactor::Logger<std::ostream>& logger = reactor::getLogger();
        logger.setLogLevel(reactor::LogLevel::INFO);
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
        TcpStreamType acceptor(io_context.get_executor(),
                               std::atoi(port.data()), ssl_server_context);
        EventQueue eventQueue(io_context.get_executor(), acceptor,
                              ssl_client_context, dest, rp, maxConnections);
        peventQueue = &eventQueue;

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
        measurementHandler.waitForRemoteMeasurements([&exchanger](
                                                         MeasurementHandler::
                                                             MeasurementResult
                                                                 result) {
            LOG_DEBUG("All measurements received, results:");
            if (checkMeasurementResult(result))
            {
                LOG_INFO(
                    "All measurements passed successfully. Sending certificates.");
                exchanger.sendCertificates();
            }
        });

        io_context.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
    }
    return 0;
}
