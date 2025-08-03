#include "spdm_requester.hpp"

#include "worker.hpp"

#include <map>
#include <vector>

auto makeSpdmTask(boost::asio::io_context& io, int port,
                  std::map<int, bool>& measurementStatus)
{
    auto spmdmtask = [port, &io]() {
        SpdmTcpClient client(io);

        SpdmRequester requester(client);

        if (!requester.connect("127.0.0.1", port))
        {
            LOG_ERROR("Failed to connect to responder");
            return false;
        }
        if (!requester.sayhello())
        {
            LOG_ERROR("Hello failed");
            return false;
        }
        if (!requester.init_connection())
        {
            return false;
        }
        return true;
    };
    return [task = std::move(spmdmtask), &io, port,
            &measurementStatus]() -> net::awaitable<void> {
        auto [ec, status] = co_await asyncCall<int>(io, std::move(task));
        if (ec)
        {
            LOG_ERROR("Error executing SPDM task on port {}: {}", port,
                      ec.message());
            measurementStatus[port] = false;
        }
        else
        {
            LOG_INFO("SPDM task on port {} completed successfully", port);
            measurementStatus[port] = status;
        }
    };
}
auto makeSpdmResultHandler(boost::asio::io_context& io,
                           std::map<int, bool>& measurementStatus, size_t size)
{
    return [&io, &measurementStatus, size]() -> net::awaitable<void> {
        while (measurementStatus.size() < size)
        {
            boost::asio::steady_timer timer(io, std::chrono::seconds(5));
            co_await timer.async_wait(net::use_awaitable);
        }

        for (const auto& [port, status] : measurementStatus)
        {
            if (status)
            {
                LOG_INFO("Measurement on port {} succeeded", port);
            }
            else
            {
                LOG_ERROR("Measurement on port {} failed", port);
            }
        }
    };
}
int main()
{
    reactor::getLogger().setLogLevel(reactor::LogLevel::DEBUG);
    boost::asio::io_context io;
    std::map<int, bool> measurementStatus;
    std::vector<int> endPoints{2223, 2224, 2225, 2226, 2227, 2228};
    auto clientSize = endPoints.size();
    for (auto e : endPoints)
    {
        net::co_spawn(io, makeSpdmTask(io, e, measurementStatus),
                      net::detached);
    }
    net::co_spawn(io, makeSpdmResultHandler(io, measurementStatus, clientSize),
                  net::detached);

    io.run();

    return 0;
}
