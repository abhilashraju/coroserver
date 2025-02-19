#pragma once
#include "file_io.hpp"
#include "tcp_server.hpp"

#include <filesystem>
#include <fstream>
static constexpr auto DONE = "Done";
using Streamer = TimedStreamer<ssl::stream<tcp::socket>>;
namespace fs = std::filesystem;
inline std::string makeEvent(const std::string& id, const std::string& data,
                             const std::string& delim = "\r\n")
{
    return std::format("{}:{}{}", id, data, delim);
}
inline std::string makeEvent(const std::string& data)
{
    return std::format("{}\r\n", data);
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
    auto [ec, size] = co_await streamer.read(buffer, false);
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
    LOG_INFO("Recieved Header: {}", data);
    co_return std::make_pair(ec, data);
}
inline AwaitableResult<size_t> sendHeader(Streamer streamer,
                                          const std::string& data)
{
    std::string header = std::format("{}\r\n", data);
    LOG_INFO("Sending Header: {}", header);
    auto [ec, size] = co_await streamer.write(net::buffer(header), false);
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
net::awaitable<boost::system::error_code>
    sendFile(Streamer streamer, const std::string& filePath)
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
    auto fileSize = lseek(fd, 0, SEEK_END);
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
    std::array<char, 1024> data;
    while (fileSize > 0)
    {
        auto [ec, bytes] = co_await reader.read(net::buffer(data));
        if (ec && ec != boost::asio::error::eof)
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

        fileSize -= bytes;
        LOG_DEBUG("Remaining: {} to send", fileSize);
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
        int fd = ::open(filePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1)
        {
            LOG_ERROR("File: {} open failed for write", filePath);
            co_return boost::asio::error::operation_aborted;
        }
        AsyncFileWriter writer(co_await net::this_coro::executor, fd);
        std::array<char, 1024> data;

        while (size > 0)
        {
            auto [ec, bytes] = co_await readData(streamer, net::buffer(data));
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
            size -= bytes;
            LOG_DEBUG("Remaining : {} bytes to recieve", size);
        }
        co_return boost::system::error_code{};
    }
    else
    {
        LOG_ERROR("Failed to read header: {}", header);
    }
    co_return boost::system::error_code{};
}
