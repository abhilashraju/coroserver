#pragma once
#include "taskqueue.hpp"
#include "tcp_server.hpp"
#include "utilities.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace fs = std::filesystem;
inline AwaitableResult<size_t> readData(Streamer streamer,
                                        net::mutable_buffer buffer)
{
    auto [ec, size] = co_await streamer.read(buffer, false);
    if (ec)
    {
        LOG_ERROR("Error reading: {}", ec.message());
        co_return std::make_pair(ec, size);
    }
    co_return std::make_pair(ec, size);
}
inline AwaitableResult<size_t> sendData(Streamer streamer,
                                        net::const_buffer buffer)
{
    auto [ec, size] = co_await streamer.write(buffer, false);
    if (ec)
    {
        LOG_ERROR("Error writing: {}", ec.message());
        co_return std::make_pair(ec, size);
    }
    co_return std::make_pair(ec, size);
}
inline AwaitableResult<std::string> readHeader(Streamer streamer)
{
    auto [ec, data] = co_await streamer.readUntil("\r\n", 1024, false);
    if (ec)
    {
        LOG_DEBUG("Error reading: {}", ec.message());
        co_return std::make_pair(ec, data);
    }
    data.erase(data.length() - 2, 2);
    LOG_INFO("Header: {}", data);
    co_return std::make_pair(ec, data);
}
inline AwaitableResult<size_t> sendHeader(Streamer streamer,
                                          const std::string& data)
{
    co_await sendData(streamer, net::buffer(""));
    std::string header = std::format("{}\r\n", data);
    co_return co_await streamer.write(net::buffer(header), false);
}
net::awaitable<boost::system::error_code> sendDone(Streamer streamer)
{
    auto [ec, size] = co_await sendHeader(streamer, "Done");
    co_return ec;
}
net::awaitable<boost::system::error_code>
    sendFile(Streamer streamer, const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        LOG_ERROR("File not found: {}", filePath);
        auto [ec, size] = co_await sendHeader(streamer, "FileNotFound:");
        co_return ec ? ec : boost::system::error_code{};
    }
    file.seekg(0, std::ios::end);
    auto fileSize = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    auto [ec, size] =
        co_await sendHeader(streamer, std::format("Content-Size:{}", fileSize));
    if (ec)
    {
        LOG_ERROR("Failed to write to stream: {}", ec.message());
        co_return ec;
    }
    std::array<char, 1024> data;
    while (true)
    {
        file.read(data.data(), data.size());
        if (file.eof())
        {
            co_await sendData(streamer, net::buffer(data, file.gcount()));
            break;
        }
        co_await sendData(streamer, net::buffer(data));
    }
    co_return boost::system::error_code{};
}
net::awaitable<boost::system::error_code>
    recieveFile(Streamer streamer, const std::string& filePath)
{
    auto [ec, header] = co_await readHeader(streamer);
    if (header.find("FileNotFound") != std::string::npos)
    {
        LOG_ERROR("File not found: {}", filePath);
        co_return boost::system::error_code{};
    }
    if (header.find("Content-Size") != std::string::npos)
    {
        auto size = std::stoull(header.substr(header.find(':') + 1));
        if (size == 0)
        {
            co_return boost::system::error_code{};
        }
        if (!fs::exists(std::filesystem::path(filePath).parent_path()))
        {
            fs::create_directories(
                std::filesystem::path(filePath).parent_path());
        }
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            LOG_ERROR("File not found: {}", filePath);
            co_return boost::system::error_code{};
        }
        std::array<char, 1024> data;
        while (size > 0)
        {
            auto [ec, bytes] = co_await readData(streamer, net::buffer(data));
            if (ec)
            {
                LOG_ERROR("Failed to read from stream: {}", ec.message());
                co_return ec;
            }
            file.write(data.data(), bytes);
            size -= bytes;
        }
        co_return boost::system::error_code{};
    }
    else
    {
        LOG_ERROR("Failed to read header: {}", header);
    }
    co_return boost::system::error_code{};
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
    std::string getEventId(std::string_view event)
    {
        if (event.find(':') != std::string::npos)
        {
            auto vw = event.substr(0, event.find(':'));
            return std::string(vw);
        }
        return event.data();
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
        events.erase(id);
        ec = co_await provider.get()(streamer, header);

        if (ec)
        {
            LOG_ERROR("Failed to handle event ret:{} {}", header, ec.message());
            co_return ec;
        }
        std::string done;
        co_await readHeader(streamer);
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
        auto consumerId = getEventId(header);
        auto handler_it = eventConsumers.find(consumerId);
        if (handler_it != eventConsumers.end())
        {
            auto ec = co_await handler_it->second(streamer, header.data());
            if (ec)
            {
                LOG_ERROR("Failed to handle event: {}", ec.message());
                co_return ec;
            }

            auto [ec1, size] = co_await sendHeader(streamer, "Done");
            co_return ec1;
        }
        auto [ec, size] = co_await sendHeader(streamer, "ConsumerNotFound");
        if (ec)
        {
            LOG_ERROR("Failed to write to stream: {}", ec.message());
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
};
