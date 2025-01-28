#pragma once
#include "tcp_client.hpp"

#include <deque>
using Streamer = TimedStreamer<ssl::stream<tcp::socket>>;
class MessageQueue
{
    using Task = std::function<net::awaitable<void>(Streamer)>;
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
        TcpClient& get()
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
    struct Message
    {
        Task messageHandler;
        std::reference_wrapper<Client> client;
        bool empty() const
        {
            return !messageHandler;
        }
    };

  public:
    MessageQueue(net::any_io_executor ioContext, net::ssl::context& sslContext,
                 const std::string& url, const std::string& port) :
        endPoint{url, port}, sslContext(sslContext), ioContext(ioContext)
    {}
    void addMessage(Task messageHandler)
    {
        bool empty = messagesHandlers.empty();
        messagesHandlers.emplace_back(std::move(messageHandler));
        if (empty)
        {
            net::co_spawn(ioContext,
                          std::bind_front(&MessageQueue::processMessages, this),
                          net::detached);
        }
    }
    net::awaitable<void> handleMessage(Message message)
    {
        auto steamer = message.client.get().get().streamer();
        co_await message.messageHandler(steamer);
        if (steamer.isOpen())
        {
            message.client.get().release();
            co_return;
        }
        removeClient(message.client);
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
    net::awaitable<void> processMessages()
    {
        while (true)
        {
            auto messageEntry = co_await getMessage();
            if (messageEntry)
            {
                co_spawn(ioContext,
                         std::bind_front(&MessageQueue::handleMessage, this,
                                         std::move(*messageEntry)),
                         net::detached);
            }
        }

        co_return;
    }
    net::awaitable<void> waitForMessage()
    {
        while (messagesHandlers.empty())
        {
            co_await net::post(co_await net::this_coro::executor,
                               net::use_awaitable);
        }
        co_return;
    }
    net::awaitable<std::optional<Message>> getMessage()
    {
        co_await waitForMessage();

        auto client = co_await getAvailableClient();
        if (client)
        {
            auto message = std::move(messagesHandlers.front());
            messagesHandlers.pop_front();
            co_return Message{message, *client};
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
                    client->get();
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
        auto ec = co_await tryConnect(client->get());
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

    std::deque<Task> messagesHandlers;
    std::vector<std::unique_ptr<Client>> clients;
    EndPoint endPoint;
    net::ssl::context& sslContext;
    net::any_io_executor ioContext;
    int maxRetryCount{3};
    size_t maxClients{3};
};
