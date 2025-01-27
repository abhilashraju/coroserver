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

    net::awaitable<void> processMessages()
    {
        while (!messagesHandlers.empty())
        {
            auto client = co_await getAvailableClient();
            if (client)
            {
                auto message = std::move(messagesHandlers.front());
                messagesHandlers.pop_front();
                co_await message((*client).get().get().streamer());
                (*client).get().release();
            }
        }
    }

  private:
    net::awaitable<std::optional<std::reference_wrapper<Client>>>
        getAvailableClient()
    {
        for (auto& client : clients)
        {
            if (client.isAvailable())
            {
                client.get();
                co_return std::ref(client);
            }
        }
        auto& client = clients.emplace_back(ioContext, sslContext);
        auto ec = co_await tryConnect(client.get());
        if (ec)
        {
            client.release();
            co_return std::nullopt;
        }
        co_return std::ref(client);
    }
    net::awaitable<boost::system::error_code> tryConnect(TcpClient& client)
    {
        net::steady_timer timer(ioContext);
        timer.expires_after(30s);
        int i = 0;
        while (i < maxRetryCount)
        {
            auto ec = co_await client.connect(endPoint.url, endPoint.port);
            if (!ec)
            {
                co_return ec;
            }
            co_await timer.async_wait(net::use_awaitable);
            i++;
        }

        co_return boost::system::errc::make_error_code(
            boost::system::errc::connection_refused);
    }

    std::deque<Task> messagesHandlers;
    std::vector<Client> clients;
    EndPoint endPoint;
    net::ssl::context& sslContext;
    net::any_io_executor ioContext;
    int maxRetryCount{3};
};
