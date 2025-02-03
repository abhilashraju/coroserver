#pragma once
#include "tcp_server.hpp"

#include <filesystem>
#include <fstream>
using Streamer = TimedStreamer<ssl::stream<tcp::socket>>;
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
