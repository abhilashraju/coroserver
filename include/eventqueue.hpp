#pragma once
#include "taskqueue.hpp"
#include "tcp_server.hpp"
#include "utilities.hpp"

#include <map>
#include <set>

inline AwaitableResult<std::string> readHeader(Streamer streamer)
{
    auto [ec, data] = co_await streamer.readUntil("\r\n", 1024, false);
    if (ec)
    {
        LOG_ERROR("Error reading: {}", ec.message());
        co_return std::make_pair(ec, data);
    }
    data.erase(data.length() - 2, 2);
    co_return std::make_pair(ec, data);
}
struct EventQueue
{
    using EventProvider =
        std::function<net::awaitable<boost::system::error_code>(
            Streamer streamer, const std::string&)>;
    using EventConsumer =
        std::function<net::awaitable<boost::system::error_code>(
            Streamer streamer, const std::string&)>;
    EventQueue(net::any_io_executor ioContext, TcpStreamType& acceptor,
               net::ssl::context& sslClientContext, const std::string& url,
               const std::string& port) :
        taskQueue(ioContext, sslClientContext, url, port),
        tcpServer(ioContext, acceptor, *this)
    {}
    void addEventProvider(const std::string& eventId,
                          EventProvider eventProvider)
    {
        eventProviders[eventId] = std::move(eventProvider);
    }
    void addEventConsumer(const std::string& eventId, EventConsumer consumer)
    {
        eventConsumers[eventId] = std::move(consumer);
    }
    u_int64_t epocNow()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }
    std::string getEventId(const std::string& event)
    {
        return event.find(':') != std::string::npos
                   ? event.substr(0, event.find(':'))
                   : event;
    }
    net::awaitable<boost::system::error_code> sendEventHandler(
        uint64_t id, std::reference_wrapper<EventProvider> provider,
        const std::string& event, Streamer streamer)
    {
        auto [ec, size] = co_await streamer.write(net::buffer(event));
        if (ec)
        {
            LOG_ERROR("Failed to write to stream: {}", ec.message());
            resendEvent(id, provider);
            co_return ec;
        }
        std::string header;
        std::tie(ec, header) = co_await readHeader(streamer);
        if (ec)
        {
            LOG_ERROR("Failed to read header: {}", ec.message());
            resendEvent(id, provider);
            co_return ec;
        }
        events.erase(id);
        ec = co_await provider.get()(streamer, header);
        if (ec)
        {
            LOG_ERROR("Failed to handle event ret:{} {}", header, ec.message());
            co_return ec;
        }
        co_return boost::system::error_code{};
    }
    void resendEvent(uint64_t id,
                     std::reference_wrapper<EventProvider> provider)
    {
        const auto& event = events[id];
        taskQueue.addTask(std::bind_front(&EventQueue::sendEventHandler, this,
                                          id, provider, event));
    }
    void addEvent(const std::string& event)
    {
        auto uniqueEventId = epocNow();
        events[uniqueEventId] = event;
        auto eventId = getEventId(event);
        auto it = eventProviders.find(eventId);
        if (it != eventProviders.end())
        {
            std::reference_wrapper<EventProvider> provider(it->second);
            taskQueue.addTask(
                std::bind_front(&EventQueue::sendEventHandler, this,
                                uniqueEventId, provider, event));
        }
    }

    inline net::awaitable<boost::system::error_code>
        parseAndHandle(std::string_view header, Streamer streamer)
    {
        auto handler_it = eventConsumers.find(header.data());
        if (handler_it != eventConsumers.end())
        {
            auto ec = co_await handler_it->second(streamer, header.data());
            if (ec)
            {
                LOG_ERROR("Failed to handle event: {}", ec.message());
                co_return ec;
            }
            co_return boost::system::error_code{};
        }
        // std::string ret = std::string(header);
        auto [ec, size] =
            co_await streamer.write(net::buffer("ConsumerNotFound\r\n"));
        if (ec)
        {
            LOG_ERROR("Failed to write to stream: {}", ec.message());
            co_return ec;
        }
        co_return boost::system::errc::make_error_code(
            boost::system::errc::no_such_file_or_directory);
    }
    inline net::awaitable<boost::system::error_code> next(Streamer streamer)
    {
        auto [ec, data] = co_await readHeader(streamer);
        if (ec)
        {
            co_return ec;
        }
        co_return co_await parseAndHandle(data, streamer);
    }
    net::awaitable<void>
        operator()(std::shared_ptr<TcpStreamType::stream_type> socket)
    {
        auto streamer = Streamer(*socket, std::make_shared<net::steady_timer>(
                                              socket->get_executor()));
        while (true)
        {
            auto ec = co_await next(streamer);
            if (ec == boost::system::errc::no_such_file_or_directory)
            {
                LOG_INFO("No Consumer found for event");
                continue;
            }
            else if (ec == boost::asio::error::operation_aborted)
            {
                LOG_INFO("Operation Aborted");
                continue;
            }
            else if (ec)
            {
                LOG_ERROR("Failed to handle client: {}", ec.message());
                co_return;
            }
        }
    }

    std::map<std::string, EventProvider> eventProviders;
    std::map<std::string, EventConsumer> eventConsumers;
    std::map<uint64_t, std::string> events;
    TaskQueue taskQueue;
    TcpServer<TcpStreamType, EventQueue> tcpServer;
};
