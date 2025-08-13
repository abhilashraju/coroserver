#pragma once
#include "beastdefs.hpp"

#include <linux/mctp.h>
namespace NSNAME
{
namespace net = boost::asio;
constexpr uint8_t messageType = 0x7E;
struct MCTPRequester
{
    static constexpr uint8_t msgType = messageType;
    boost::asio::generic::datagram_protocol::socket mctpSocket;
    boost::asio::steady_timer expiryTimer;

    boost::asio::generic::datagram_protocol::endpoint recvEndPoint;
    MCTPRequester(boost::asio::io_context& ctx) :
        mctpSocket(ctx, boost::asio::generic::datagram_protocol{AF_MCTP, 0}),
        expiryTimer(ctx)
    {}

    // Example method signatures:
    net::awaitable<boost::system::error_code> sendMessage(
        uint8_t eid, const std::span<const uint8_t> message)
    {
        struct sockaddr_mctp addr{};
        addr.smctp_family = AF_MCTP;
        addr.smctp_network = MCTP_NET_ANY;
        addr.smctp_addr.s_addr = eid;
        addr.smctp_type = msgType;
        addr.smctp_tag = MCTP_TAG_OWNER;

        boost::asio::generic::datagram_protocol::endpoint sendEndPoint = {
            &addr, sizeof(addr)};
        boost::system::error_code ec;
        co_await mctpSocket.async_send_to(
            boost::asio::const_buffer(message.data(), message.size()),
            sendEndPoint, boost::asio::redirect_error(net::use_awaitable, ec));
        co_return ec;
    }
    net::awaitable<boost::system::error_code> receiveMessage(
        std::span<uint8_t> response)
    {
        boost::system::error_code ec;
        co_await mctpSocket.async_receive_from(
            boost::asio::mutable_buffer(response.data(), response.size()),
            recvEndPoint, boost::asio::redirect_error(net::use_awaitable, ec));

        co_return ec;
    }
};
}