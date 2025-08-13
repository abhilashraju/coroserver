#pragma once

#include "beastdefs.hpp"
#include "logger.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <boost/asio/posix/stream_descriptor.hpp>

#include <iostream>
#include <string>
namespace NSNAME
{
class AsyncFileWriter
{
  public:
    AsyncFileWriter(net::any_io_executor io_context, int fd) :
        stream_(io_context, fd), fd_(fd)
    {}
    ~AsyncFileWriter()
    {
        // Close the file descriptor when the object is destroyed
        if (fd_ != -1)
        {
            ::close(fd_);
        }
    }

    net::awaitable<boost::system::error_code> write(net::const_buffer data)
    {
        // Start an asynchronous write operation
        co_return co_await async_write_some(data);
    }

  private:
    net::awaitable<boost::system::error_code> async_write_some(
        net::const_buffer data)
    {
        // Write data asynchronously
        boost::system::error_code ec;
        co_await stream_.async_write_some(
            data, net::redirect_error(net::use_awaitable, ec));
        if (ec)
        {
            LOG_ERROR("Error writing to file: {}", ec.message());
            co_return ec;
        }
        co_return boost::system::error_code{};
    }

    net::posix::stream_descriptor stream_;
    int fd_{-1};
};
class AsyncFileReader
{
  public:
    AsyncFileReader(net::any_io_executor io_context, int fd) :
        stream_(io_context, fd), fd_(fd)
    {}
    ~AsyncFileReader()
    {
        // Close the file descriptor when the object is destroyed
        if (fd_ != -1)
        {
            ::close(fd_);
        }
    }

    net::awaitable<std::pair<boost::system::error_code, std::size_t>> read(
        net::mutable_buffer buffer)
    {
        // Start an asynchronous read operation
        co_return co_await async_read_some(buffer);
    }

  private:
    net::awaitable<std::pair<boost::system::error_code, std::size_t>>
        async_read_some(net::mutable_buffer buffer)
    {
        // Read data asynchronously
        boost::system::error_code ec;
        std::size_t bytes_transferred = co_await stream_.async_read_some(
            buffer, net::redirect_error(net::use_awaitable, ec));
        if (ec)
        {
            LOG_ERROR("Error reading from file: {}", ec.message());
            co_return std::make_pair(ec, 0);
        }
        co_return std::make_pair(boost::system::error_code{},
                                 bytes_transferred);
    }

    net::posix::stream_descriptor stream_;
    int fd_{-1};
};
} // namespace NSNAME
