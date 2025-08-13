#pragma once
#include "beastdefs.hpp"
#include "logger.hpp"
#include "make_awaitable.hpp"
namespace NSNAME
{
struct PacketStreamType
{
    using stream_type = boost::asio::generic::datagram_protocol::socket;
    stream_type socket_;
    boost::asio::generic::datagram_protocol::endpoint endpoint;

    PacketStreamType(
        boost::asio::io_context& ctx,
        const boost::asio::generic::datagram_protocol::endpoint& ep) :
        socket_(ctx, ep.protocol()), endpoint(ep)
    {
        LOG_DEBUG("PacketStreamType created with endpoint: {}",
                  endpoint.address().to_string());
    }

    net::awaitable<boost::system::error_code> send(
        const std::span<const uint8_t> data)
    {
        boost::system::error_code ec;
        co_await socket_.async_send_to(
            boost::asio::const_buffer(data.data(), data.size()), endpoint,
            boost::asio::redirect_error(net::use_awaitable, ec));
        co_return ec;
    }
    net::awaitable<boost::system::error_code> receive(std::span<uint8_t> buffer)
    {
        boost::system::error_code ec;
        co_await socket_.async_receive_from(
            boost::asio::mutable_buffer(buffer.data(), buffer.size()), endpoint,
            boost::asio::redirect_error(net::use_awaitable, ec));
        co_return ec;
    }
};
}