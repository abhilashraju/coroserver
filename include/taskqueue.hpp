#pragma once
#include "tcp_client.hpp"

#include <deque>
using Streamer = TimedStreamer<ssl::stream<tcp::socket>>;
class TaskQueue
{
    using Task =
        std::function<net::awaitable<boost::system::error_code>(Streamer)>;
    struct Client
    {
        TcpClient client;
        bool available = true;
        Client(net::any_io_executor ioContext, net::ssl::context& sslContext) :
            client(ioContext, sslContext)
        {}
        ~Client()
        {
            // client.close();
        }
        bool isAvailable() const
        {
            return available;
        }
        TcpClient& acquire()
        {
            available = false;
            return client;
        }
        bool operator==(TcpClient& other) const
        {
            return &client == &other;
        }
        void release()
        {
            available = true;
        }
    };
    struct EndPoint
    {
        std::string url;
        std::string port;
    };
    struct NetworkTask
    {
        Task task;
        std::reference_wrapper<Client> client;
        bool empty() const
        {
            return !task;
        }
    };

  public:
    TaskQueue(net::any_io_executor ioContext, net::ssl::context& sslContext,
              const std::string& url, const std::string& port,
              int maxConnections = 1) :
        endPoint{url, port}, sslContext(sslContext), ioContext(ioContext),
        maxClients(maxConnections)
    {}
    void addTask(Task messageHandler)
    {
        bool empty = taskHandlers.empty();
        taskHandlers.emplace_back(std::move(messageHandler));
        if (empty)
        {
            net::co_spawn(ioContext,
                          std::bind_front(&TaskQueue::processTasks, this),
                          net::detached);
        }
    }
    net::awaitable<void> handleTask(NetworkTask netTask)
    {
        auto steamer = netTask.client.get().acquire().streamer();
        auto ec = co_await netTask.task(steamer);
        if (!ec)
        {
            netTask.client.get().release();
            co_return;
        }
        removeClient(netTask.client);
        // the connection is closed, we need to remove the client
    }
    void removeClient(std::reference_wrapper<Client> client)
    {
        clients.erase(std::remove_if(clients.begin(), clients.end(),
                                     [&client](auto& c) {
                                         return c.get() == &client.get();
                                     }),
                      clients.end());
    }
    net::awaitable<void> processTasks()
    {
        while (true)
        {
            auto taskEntry = co_await getTask();
            if (taskEntry)
            {
                co_spawn(ioContext,
                         std::bind_front(&TaskQueue::handleTask, this,
                                         std::move(*taskEntry)),
                         net::detached);
            }
        }

        co_return;
    }
    net::awaitable<void> waitForTask()
    {
        while (taskHandlers.empty())
        {
            co_await net::post(co_await net::this_coro::executor,
                               net::use_awaitable);
        }
        co_return;
    }
    net::awaitable<std::optional<NetworkTask>> getTask()
    {
        co_await waitForTask();

        auto client = co_await getAvailableClient();

        if (client)
        {
            if (taskHandlers.empty())
            {
                client.value().get().release();
                co_return std::nullopt;
            }
            auto message = std::move(taskHandlers.front());
            taskHandlers.pop_front();
            co_return NetworkTask{message, *client};
        }
        co_return std::nullopt;
    }

  private:
    net::awaitable<std::optional<std::reference_wrapper<Client>>>
        waitForClient()
    {
        while (clients.size() >= maxClients)
        {
            for (auto& client : clients)
            {
                if (client->isAvailable())
                {
                    client->acquire();
                    co_return std::ref(*client);
                }
            }
            co_await net::post(co_await net::this_coro::executor,
                               net::use_awaitable);
        }
        co_return std::nullopt;
    }
    net::awaitable<std::optional<std::reference_wrapper<Client>>>
        getAvailableClient()
    {
        auto freeclient = co_await waitForClient();
        if (freeclient)
        {
            co_return freeclient;
        }
        auto client = std::make_unique<Client>(ioContext, sslContext);
        auto ec = co_await tryConnect(client->acquire());
        if (!ec)
        {
            auto clientToRet = std::ref(*client);
            clients.emplace_back(std::move(client));
            co_return clientToRet;
        }
        co_return std::nullopt;
    }
    net::awaitable<boost::system::error_code> tryConnect(TcpClient& client)
    {
        net::steady_timer timer(ioContext);

        int i = 0;
        while (i < maxRetryCount)
        {
            auto ec = co_await client.connect(endPoint.url, endPoint.port);
            if (!ec)
            {
                co_return ec;
            }
            timer.expires_after(5s);
            co_await timer.async_wait(net::use_awaitable);
            i++;
        }

        co_return boost::system::errc::make_error_code(
            boost::system::errc::connection_refused);
    }

    std::deque<Task> taskHandlers;
    std::vector<std::unique_ptr<Client>> clients;
    EndPoint endPoint;
    net::ssl::context& sslContext;
    net::any_io_executor ioContext;
    int maxRetryCount{3};
    size_t maxClients{1};
};
