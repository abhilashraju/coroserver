#pragma once
#include "beastdefs.hpp"
#include "logger.hpp"

#include <linux/mctp.h>

#include <span>
#include <vector>
namespace NSNAME
{
namespace net = boost::asio;

constexpr uint8_t messageType = 0x7E;

struct MCTPResponder
{
    static constexpr uint8_t msgType = messageType;
    static constexpr size_t maxMessageSize = 65536 + 256;
    boost::asio::generic::datagram_protocol::socket mctpSocket;
    boost::asio::steady_timer
        expiryTimer; // Can be used for timeouts or other timed operations

    // The endpoint from which the last message was received.
    // This is used to send responses back to the original requester.
    boost::asio::generic::datagram_protocol::endpoint senderEndPoint;

    MCTPResponder(boost::asio::io_context& ctx, uint8_t localEid) :
        mctpSocket(ctx, boost::asio::generic::datagram_protocol{AF_MCTP, 0}),
        expiryTimer(ctx)
    {
        // Bind the socket to the local EID for receiving messages
        struct sockaddr_mctp addr{};
        addr.smctp_family = AF_MCTP;
        addr.smctp_addr.s_addr = localEid;
        addr.smctp_type =
            msgType; // Can be set to a specific type or 0 to receive all types
        senderEndPoint = boost::asio::generic::datagram_protocol::endpoint(
            &addr, sizeof(addr));
        // The socket needs to be bound to a local EID to receive messages
        // destined for it.
        boost::system::error_code ec;
        mctpSocket.bind(senderEndPoint, ec);
        if (ec)
        {
            LOG_ERROR("Failed to bind MCTP socket: {}", ec.message());
        }
    }

    net::awaitable<std::tuple<boost::system::error_code, size_t>>
        receiveMessage(std::span<uint8_t> buffer)
    {
        boost::system::error_code ec;
        size_t bytesReceived = co_await mctpSocket.async_receive_from(
            boost::asio::mutable_buffer(buffer.data(), buffer.size()),
            senderEndPoint,
            boost::asio::redirect_error(net::use_awaitable, ec));

        co_return std::make_tuple(ec, bytesReceived);
    }

    net::awaitable<boost::system::error_code> sendResponse(
        const std::span<const uint8_t> response, uint8_t tagOwner,
        uint8_t messageTag)
    {
        boost::system::error_code ec;
        co_await mctpSocket.async_send_to(
            boost::asio::const_buffer(response.data(), response.size()),
            senderEndPoint,
            boost::asio::redirect_error(net::use_awaitable, ec));
        co_return ec;
    }
};
}