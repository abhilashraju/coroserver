#pragma once
#include "file_io.hpp"
#include "tcp_server.hpp"

#include <filesystem>
#include <fstream>
static constexpr auto DONE = "Done";
static constexpr auto HEDER_DELIM = "\r\n\r\n";
static constexpr auto BUFFER_SIZE = 8192;
using Streamer = TimedStreamer<ssl::stream<tcp::socket>>;
namespace fs = std::filesystem;
static constexpr auto timeoutneeded = true;
inline u_int64_t epocNow()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
inline std::string currentTime()
{
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;
    auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                      now.time_since_epoch()) %
                  1000;

    std::tm tm = *std::localtime(&now_time_t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S") << '.' << std::setw(3)
        << std::setfill('0') << now_ms.count() << '.' << std::setw(3)
        << std::setfill('0') << now_us.count();
    return oss.str();
}
inline std::string makeEvent(const std::string& id, const std::string& data,
                             const std::string& delim = HEDER_DELIM)
{
    return std::format("{}:{}{}", id, data, delim);
}
inline std::string makeEvent(const std::string& data)
{
    return std::format("{}{}", data, HEDER_DELIM);
}
inline std::pair<std::string, std::string> parseEvent(const std::string& event)
{
    auto pos = event.find(':');
    if (pos == std::string::npos)
    {
        return {event, ""};
    }
    return {event.substr(0, pos), event.substr(pos + 1)};
}
inline AwaitableResult<size_t> readData(Streamer streamer,
                                        net::mutable_buffer buffer)
{
    auto [ec, size] = co_await streamer.read(buffer, timeoutneeded);
    if (ec)
    {
        LOG_DEBUG("Error reading: {}", ec.message());
        co_return std::make_pair(ec, size);
    }
    co_return std::make_pair(ec, size);
}
inline AwaitableResult<size_t> sendData(Streamer streamer,
                                        net::const_buffer buffer)
{
    auto [ec, size] = co_await streamer.write(buffer, timeoutneeded);
    if (ec)
    {
        LOG_ERROR("Error writing: {}", ec.message());
        co_return std::make_pair(ec, size);
    }
    co_return std::make_pair(ec, size);
}
inline AwaitableResult<std::string> readHeader(Streamer streamer)
{
    auto [ec, data] = co_await streamer.readUntil(HEDER_DELIM, timeoutneeded);
    if (ec)
    {
        LOG_INFO("Error reading: {}", ec.message());
        co_return std::make_pair(ec, data);
    }
    auto delimLength = std::string_view(HEDER_DELIM).length();
    data.erase(data.length() - delimLength, delimLength);
    LOG_INFO("{} Recieved Header: {}", currentTime(), data);
    co_return std::make_pair(ec, data);
}
inline AwaitableResult<size_t> sendHeader(Streamer streamer,
                                          const std::string& data)
{
    std::string header = std::format("{}{}", data, HEDER_DELIM);
    LOG_INFO("{} Sending Header: {}", currentTime(), header);
    auto [ec,
          size] = co_await streamer.write(net::buffer(header), timeoutneeded);
    if (ec)
    {
        LOG_ERROR("Failed to write to stream: {}", ec.message());
    }
    co_return std::make_pair(ec, size);
}
net::awaitable<boost::system::error_code> sendDone(Streamer streamer)
{
    auto [ec, size] = co_await sendHeader(streamer, DONE);
    co_return ec;
}
net::awaitable<boost::system::error_code> sendFile(Streamer streamer,
                                                   const std::string& filePath)
{
    // std::ifstream file(filePath, std::ios::binary);
    // if (!file.is_open())
    // {
    //     LOG_ERROR("File not found: {}", filePath);
    //     auto [ec, size] = co_await sendHeader(streamer, "FileNotFound:");
    //     co_return ec ? ec : boost::system::error_code{};
    // }
    // file.seekg(0, std::ios::end);
    // auto fileSize = static_cast<std::size_t>(file.tellg());
    // file.seekg(0, std::ios::beg);
    int fd = ::open(filePath.c_str(), O_RDONLY);
    if (fd == -1)
    {
        LOG_ERROR("File: {} open failed for read", filePath);
        co_return boost::asio::error::operation_aborted;
    }
    off_t fileSize = lseek(fd, 0, SEEK_END);
    if (fileSize == -1)
    {
        LOG_ERROR("Failed to get file size: {}", filePath);
        co_return boost::asio::error::operation_aborted;
    }
    lseek(fd, 0, SEEK_SET);
    AsyncFileReader reader(co_await net::this_coro::executor, fd);

    auto [ec, size] =
        co_await sendHeader(streamer, std::format("Content-Size:{}", fileSize));
    if (ec)
    {
        LOG_ERROR("Failed to write to stream: {}", ec.message());
        co_return ec;
    }
    std::vector<char> data((fileSize < BUFFER_SIZE) ? fileSize : BUFFER_SIZE);
    off_t sentSofar = 0;
    while (sentSofar < fileSize)
    {
        auto [ec, bytes] =
            co_await reader.read(net::buffer(data.data(), data.size()));
        if (ec == boost::asio::error::eof)
        {
            break;
        }
        if (ec)
        {
            LOG_ERROR("Failed to read from file: {}", ec.message());
            co_return ec;
        }
        size_t byteSent = 0;
        std::tie(ec, byteSent) =
            co_await sendData(streamer, net::buffer(data.data(), bytes));
        if (ec)
        {
            LOG_ERROR("Failed to write to stream: {}", ec.message());
            co_return ec;
        }

        sentSofar += bytes;
        LOG_DEBUG("Remaining: {} to send", fileSize - sentSofar);
    }
    co_return boost::system::error_code{};
}
AwaitableResult<std::string> readFor(Streamer streamer, const auto& headers,
                                     int retryCount = 3)
{
    std::string header;
    boost::system::error_code ec;
    do
    {
        std::tie(ec, header) = co_await readHeader(streamer);
        auto iter = std::ranges::find_if(headers, [&header](const auto& h) {
            return header.find(h) != std::string::npos;
        });
        if (iter != headers.end())
        {
            co_return std::make_pair(boost::system::error_code{}, header);
        }
    } while (!ec && --retryCount > 0);
    co_return std::make_pair(ec, "");
}
net::awaitable<boost::system::error_code> recieveFile(
    Streamer streamer, const std::string& filePath)
{
    auto [ec, header] = co_await readFor(
        streamer,
        std::vector{"FileNotFound", "Content-Size"}); // to fix read until error
    if (ec)

        if (header.find("FileNotFound") != std::string::npos)
        {
            LOG_ERROR("File not found: {}", filePath);
            co_return boost::system::error_code{};
        }
    if (header.find("Content-Size") != std::string::npos)
    {
        std::size_t fileSize = std::stoull(header.substr(header.find(':') + 1));
        if (fileSize == 0)
        {
            co_return boost::system::error_code{};
        }
        if (!fs::exists(std::filesystem::path(filePath).parent_path()))
        {
            fs::create_directories(
                std::filesystem::path(filePath).parent_path());
        }
        int fd = ::open(filePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1)
        {
            LOG_ERROR("File: {} open failed for write", filePath);
            co_return boost::asio::error::operation_aborted;
        }
        AsyncFileWriter writer(co_await net::this_coro::executor, fd);
        std::vector<char> data(
            (fileSize < BUFFER_SIZE) ? fileSize : BUFFER_SIZE);
        std::size_t recSofar = 0;
        while (recSofar < fileSize)
        {
            auto [ec, bytes] = co_await readData(
                streamer, net::buffer(data.data(), data.size()));
            if (ec)
            {
                LOG_ERROR("Failed to read from stream: {}", ec.message());
                co_return ec;
            }
            ec = co_await writer.write(net::buffer(data.data(), bytes));

            if (ec)
            {
                LOG_ERROR("Failed to write to file: {}", ec.message());
                co_return ec;
            }
            recSofar += bytes;
            LOG_DEBUG("Remaining : {} bytes recieved", fileSize - recSofar);
        }
        co_return boost::system::error_code{};
    }
    else
    {
        LOG_ERROR("Failed to read header: {}", header);
        co_return boost::system::error_code{
            boost::system::errc::operation_not_supported,
            boost::system::system_category()};
    }
    co_return boost::system::error_code{};
}
