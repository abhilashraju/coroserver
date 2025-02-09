#pragma once
#include "serializer.hpp"
#include "taskqueue.hpp"
#include "tcp_server.hpp"
#include "utilities.hpp"

#include <map>
static constexpr auto EVENTQUEFILE = "/var/lib/coroserver/eventqueue.dat";
namespace fs = std::filesystem;
struct EventQueue
{
    using EventProvider =
        std::function<net::awaitable<boost::system::error_code>(
            Streamer streamer, const std::string&)>;
    using EventConsumer =
        std::function<net::awaitable<boost::system::error_code>(
            Streamer streamer, const std::string&)>;
    struct DefaultEventProvider
    {
        net::awaitable<boost::system::error_code>
            operator()(Streamer streamer, const std::string& event)
        {
            LOG_WARNING("Received event in defualt provider: {}", event);
            if (event == DONE)
            {
                co_return boost::asio::error::connection_reset;
            }
            co_return boost::system::error_code{};
        }
    };
    struct DefaultEventConsumer
    {
        net::awaitable<boost::system::error_code>
            operator()(Streamer streamer, const std::string& event)
        {
            LOG_WARNING("Received event in defualt Consumer: {}", event);
            // co_await sendHeader(streamer, "ConsumerNotFound");
            co_return boost::system::error_code{};
        }
    };
    EventQueue(net::any_io_executor ioContext, TcpStreamType& acceptor,
               net::ssl::context& sslClientContext, const std::string& url,
               const std::string& port, int maxConnections = 1) :
        taskQueue(ioContext, sslClientContext, url, port, maxConnections),
        tcpServer(ioContext, acceptor, *this), serializer(EVENTQUEFILE)
    {
        addEventProvider("default", defaultProvider);
        addEventConsumer("default", defaultConsumer);
    }
    void addEventProvider(const std::string& eventId,
                          EventProvider eventProvider)
    {
        eventProviders[eventId] = std::move(eventProvider);
    }
    void addEventConsumer(const std::string& eventId, EventConsumer consumer)
    {
        eventConsumers[eventId] = std::move(consumer);
    }
    std::optional<std::string> readFile(const std::string& filePath)
    {
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            LOG_ERROR("Failed to open file: {}", filePath);
            return std::nullopt;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
    void load()
    {
        if (!fs::exists(EVENTQUEFILE))
        {
            LOG_INFO("No event queue file found");
            return;
        }
        serializer.load();
        std::vector<std::string> events;
        serializer.deserialize("events", events);
        for (auto& event : events)
        {
            auto eventmap = split(event, ',');
            uint64_t id = std::stoull(std::string(eventmap[0]));
            addEvent(event, id);
        }
    }
    void store()
    {
        std::vector<std::string> events;
        for (auto& [id, event] : this->events)
        {
            events.push_back(std::to_string(id) + "," + event);
        }
        serializer.serialize("events", events);
        serializer.store();
    }

    u_int64_t epocNow()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }
    std::string getEventId(std::string_view event)
    {
        if (event.find(':') != std::string::npos)
        {
            auto vw = event.substr(0, event.find(':'));
            return std::string(vw);
        }
        return event.data();
    }
    void removeEvent(uint64_t id)
    {
        events.erase(id);
        store();
    }
    net::awaitable<boost::system::error_code>
        executeProvider(std::reference_wrapper<EventProvider> provider,
                        Streamer streamer, const std::string& event)
    {
        try
        {
            co_return co_await provider(streamer, event);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to handle event: {}", e.what());
            co_return boost::asio::error::connection_reset;
        }
    }

    net::awaitable<boost::system::error_code> sendEventHandler(
        uint64_t id, std::reference_wrapper<EventProvider> provider,
        const std::string& event, Streamer streamer)
    {
        auto [ec, size] = co_await streamer.write(net::buffer(event), false);
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
        removeEvent(id);
        ec = co_await executeProvider(provider, streamer, header);

        if (ec)
        {
            co_return ec;
        }

        auto [ec1, done] = co_await readHeader(streamer);
        co_return boost::asio::error::connection_reset;
        // co_return ec;
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
        addEvent(event, uniqueEventId);
        store();
    }
    void addEvent(const std::string& event, uint64_t id)
    {
        events[id] = event;
        auto eventId = getEventId(event);
        auto it = eventProviders.find(eventId);
        if (it == eventProviders.end())
        {
            it = eventProviders.find("default");
        }
        std::reference_wrapper<EventProvider> provider(it->second);
        taskQueue.addTask(std::bind_front(&EventQueue::sendEventHandler, this,
                                          id, provider, event));
    }
    bool eventExists(const std::string& event)
    {
        for (auto& [id, e] : events)
        {
            if (e == event)
            {
                return true;
            }
        }
        return false;
    }
    net::awaitable<boost::system::error_code>
        executeConsumer(std::reference_wrapper<EventConsumer> consumer,
                        Streamer streamer, const std::string& event)
    {
        try
        {
            co_return co_await consumer(streamer, event);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to handle event: {}", e.what());
            co_return boost::asio::error::connection_reset;
        }
    }

    inline net::awaitable<boost::system::error_code>
        parseAndHandle(std::string_view header, Streamer streamer)
    {
        auto consumerId = getEventId(header);
        auto handler_it = eventConsumers.find(consumerId);
        if (handler_it == eventConsumers.end())
        {
            handler_it = eventConsumers.find("default");
        }
        auto ec = co_await executeConsumer(handler_it->second, streamer,
                                           header.data());
        if (ec)
        {
            LOG_ERROR("Failed to handle event: {}", ec.message());
            co_return ec;
        }
        co_return co_await sendDone(streamer);
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
            if (ec == boost::asio::error::operation_aborted)
            {
                LOG_INFO("Operation Aborted");
                continue;
            }
            else if (ec)
            {
                LOG_DEBUG("Failed to handle client: {}", ec.message());
                co_return;
            }
        }
    }

    std::map<std::string, EventProvider> eventProviders;
    std::map<std::string, EventConsumer> eventConsumers;
    std::map<uint64_t, std::string> events;
    TaskQueue taskQueue;
    TcpServer<TcpStreamType, EventQueue> tcpServer;
    DefaultEventProvider defaultProvider;
    DefaultEventConsumer defaultConsumer;
    JsonSerializer serializer;
};
