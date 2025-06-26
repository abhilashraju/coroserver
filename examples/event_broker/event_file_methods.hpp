#pragma once
#include "eventmethods.hpp"
net::awaitable<boost::system::error_code> sendFile(Streamer streamer,
                                                   const std::string& filePath)
{
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
    Streamer streamer, const std::string& root, const std::string& destPath);
net::awaitable<boost::system::error_code> recieveArchiveFile(
    Streamer streamer, const std::string& root, const std::string& destPath,
    const std::string& archPath)
{
    auto ec = co_await recieveFile(streamer, root, archPath);
    if (ec)
    {
        co_return ec;
    }
    if (!fs::exists(root + destPath))
    {
        fs::create_directories(root + destPath);
    }
    if (extractTarArchive(root + archPath, root + destPath))
    {
        LOG_INFO("Extracted tar file: {}", root + archPath);
        std::filesystem::remove(root + archPath);
        co_return boost::system::error_code{};
    }

    LOG_ERROR("Failed to extract tar file: {}", root + archPath);
    co_return boost::system::error_code{boost::system::errc::io_error,
                                        boost::system::system_category()};
}
net::awaitable<boost::system::error_code> recieveFile(
    Streamer streamer, const std::string& root, const std::string& destPath)
{
    std::string filePath = root + destPath;
    auto [ec, header] = co_await readFor(
        streamer, std::vector{"FileNotFound", "Content-Size",
                              "FileType"}); // to fix read until error
    if (ec)
    {
        co_return ec;
    }
    auto [id, data] = parseEvent(header);
    if (id == "FileType")
    {
        auto [ftype, archfilePath] = parseEvent(data);
        if (ftype == "archive")
        {
            co_return co_await recieveArchiveFile(streamer, root, destPath,
                                                  archfilePath);
        }
        co_return co_await recieveFile(streamer, filePath, ftype);
    }
    if (id == "FileNotFound")
    {
        LOG_ERROR("File not found: {}", filePath);
        co_return boost::system::error_code{};
    }
    if (id == "Content-Size")
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