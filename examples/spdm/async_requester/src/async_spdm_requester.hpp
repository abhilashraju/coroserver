#pragma once
/**
 * AsyncSpdmRequester — async counterpart of SpdmConnectionManager.
 *
 * Inherits SpdmConnection (context lifecycle, buffer callbacks, transport
 * registration) but does NOT register blocking device_send_message /
 * device_receive_message callbacks.  Instead the async library functions
 * (libspdm_send_request_async, libspdm_receive_response_async,
 * libspdm_init_connection_async) accept an AsyncSpdmTcpIO reference directly
 * and bypass the callback mechanism entirely.
 *
 * Usage (from a coroutine):
 *   AsyncSpdmRequester req(ioCtx);
 *   auto ec = co_await req.asyncConnect("192.168.1.1", 2323);
 *   if (!ec) {
 *       auto status = co_await req.asyncInitConnection();
 *   }
 */

#include "async_spdm_io.hpp"
#include "logger.hpp"
#include "requester_init.hpp"
#include "spdmglobal.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/system/error_code.hpp>

extern "C"
{
#include <library/spdm_requester_lib.h>
}

#include "async/libspdm_req_init_connection_async.hpp"
#include "async/libspdm_req_send_receive_async.hpp"

using boost::asio::ip::tcp;

/**
 * @brief Async SPDM requester session.
 *
 * Extends SpdmConnection which handles:
 *  - libspdm context allocation / init
 *  - transport layer registration (TCP)
 *  - device buffer registration
 *  - scratch buffer allocation
 *
 * This class adds:
 *  - async TCP connect
 *  - async SPDM init_connection (GET_VERSION → GET_CAPABILITIES →
 *    NEGOTIATE_ALGORITHMS)
 *  - a top-level run() coroutine for use with co_spawn
 */
class AsyncSpdmRequester : public SpdmConnection
{
  public:
    explicit AsyncSpdmRequester(boost::asio::io_context& ioCtx) :
        ioCtx_(ioCtx), resolver_(ioCtx),
        socket_(std::make_shared<tcp::socket>(ioCtx))
    {
        // The async path bypasses the libspdm send/receive callback mechanism
        // entirely (io is passed directly to the coroutine helpers).  However
        // libspdm still requires *some* function to be registered or it asserts
        // on first use, so register no-op stubs that should never be called.
        libspdm_register_device_io_func(spdmContext, noopSend, noopReceive);

        // Allocate and register the scratch buffer.  SpdmConnectionTemplate
        // normally does this, but AsyncSpdmRequester inherits SpdmConnection
        // directly to avoid the blocking-callback machinery, so we do it here.
        // Without the scratch buffer libspdm_get_sizeof_required_scratch_buffer
        // fires LIBSPDM_ASSERT inside libspdm_try_get_version_async and aborts.
        scratchSize = libspdm_get_sizeof_required_scratch_buffer(spdmContext);
        scratchBuffer = std::malloc(scratchSize);
        if (scratchBuffer == nullptr)
        {
            throw std::runtime_error(
                "AsyncSpdmRequester: failed to allocate scratch buffer");
        }
        libspdm_set_scratch_buffer(spdmContext, scratchBuffer, scratchSize);

        // Configure algorithm/capability local data from the global m_* settings.
        // The sync path does this inside spdmClientInit() before calling the
        // blocking libspdm_init_connection().  The async path drives the
        // connection itself via coroutines so spdmClientInit() is never called —
        // without these setLocalData calls all algorithm fields stay 0, the
        // responder negotiates empty algorithm sets, and NEGOTIATE_ALGORITHMS
        // fails with INVALID_MSG_FIELD.
        {
            uint8_t data8 = 0;
            setLocalData(spdmContext, LIBSPDM_DATA_CAPABILITY_CT_EXPONENT, data8);

            uint32_t data32 = m_use_requester_capability_flags;
            if (m_use_req_slot_id == 0xFF)
            {
                data32 |= SPDM_GET_CAPABILITIES_REQUEST_FLAGS_PUB_KEY_ID_CAP;
                data32 &= ~SPDM_GET_CAPABILITIES_REQUEST_FLAGS_CERT_CAP;
                data32 &= ~SPDM_GET_CAPABILITIES_REQUEST_FLAGS_MULTI_KEY_CAP;
            }
            if (m_use_capability_flags != 0)
                data32 = m_use_capability_flags;
            setLocalData(spdmContext, LIBSPDM_DATA_CAPABILITY_FLAGS, data32);

            data8 = m_support_measurement_spec;
            setLocalData(spdmContext, LIBSPDM_DATA_MEASUREMENT_SPEC, data8);

            data32 = m_support_asym_algo;
            setLocalData(spdmContext, LIBSPDM_DATA_BASE_ASYM_ALGO, data32);

            data32 = m_support_hash_algo;
            setLocalData(spdmContext, LIBSPDM_DATA_BASE_HASH_ALGO, data32);

            uint16_t data16 = m_support_dhe_algo;
            setLocalData(spdmContext, LIBSPDM_DATA_DHE_NAME_GROUP, data16);

            data16 = m_support_aead_algo;
            setLocalData(spdmContext, LIBSPDM_DATA_AEAD_CIPHER_SUITE, data16);

            data16 = m_support_req_asym_algo;
            setLocalData(spdmContext, LIBSPDM_DATA_REQ_BASE_ASYM_ALG, data16);

            data16 = m_support_key_schedule_algo;
            setLocalData(spdmContext, LIBSPDM_DATA_KEY_SCHEDULE, data16);

            data8 = m_support_other_params_support;
            setLocalData(spdmContext, LIBSPDM_DATA_OTHER_PARAMS_SUPPORT, data8);

            data8 = m_support_mel_spec;
            setLocalData(spdmContext, LIBSPDM_DATA_MEL_SPEC, data8);

            if (m_use_version != 0)
            {
                spdm_version_number_t ver =
                    m_use_version << SPDM_VERSION_NUMBER_SHIFT_BIT;
                setLocalData(spdmContext, LIBSPDM_DATA_SPDM_VERSION, ver);
            }
        }
    }

    AsyncSpdmRequester(const AsyncSpdmRequester&) = delete;
    AsyncSpdmRequester& operator=(const AsyncSpdmRequester&) = delete;
    AsyncSpdmRequester(AsyncSpdmRequester&&) = delete;
    AsyncSpdmRequester& operator=(AsyncSpdmRequester&&) = delete;

    /**
     * @brief Async TCP connect + optional SSL handshake.
     *
     * @param host Hostname or IP address.
     * @param port Port number as string or uint16_t.
     */
    boost::asio::awaitable<boost::system::error_code> asyncConnect(
        const std::string& host, uint16_t port)
    {
        boost::system::error_code ec;
        auto results = resolver_.resolve(host, std::to_string(port), ec);
        if (ec)
        {
            LOG_ERROR("AsyncSpdmRequester: resolve {}:{} failed: {}", host,
                      port, ec.message());
            co_return ec;
        }
        co_await boost::asio::async_connect(
            *socket_, results,
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        if (ec)
        {
            LOG_ERROR("AsyncSpdmRequester: connect {}:{} failed: {}", host,
                      port, ec.message());
        }
        LOG_DEBUG("Connected to responder");
        co_return ec;
    }

    /**
     * @brief Run the SPDM connection negotiation asynchronously.
     *
     * Calls spdmClientInit (which calls libspdm_init_connection) via the
     * async shim bridge so the io_context is never blocked.
     */
    boost::asio::awaitable<libspdm_return_t> asyncInitConnection()
    {
        LIBSPDM_ASSERT(io_ != nullptr);
        auto status = co_await libspdm_init_connection_async(spdmContext, false,
                                                             ioCtx_, *io_);
        co_return status;
    }

    /**
     * @brief Top-level coroutine: connect → negotiate → done.
     *
     * Suitable for co_spawn(ioCtx, req.run(host, port), detached).
     */
    boost::asio::awaitable<void> run(const std::string& host, uint16_t port)
    {
        co_await connectAndInit(host, port);
    }

    /**
     * @brief Connect + negotiate, returning true on success.
     *
     * Preferred entry point when the caller needs to know whether the full
     * handshake succeeded (e.g. AsyncComponentIntegrity::addComponentIntegrity).
     */
    boost::asio::awaitable<bool> connectAndInit(const std::string& host,
                                                uint16_t port)
    {
        auto ec = co_await asyncConnect(host, port);
        if (ec)
        {
            LOG_ERROR("AsyncSpdmRequester: connect failed");
            co_return false;
        }

        // Build the AsyncSpdmTcpIO from the connected socket
        io_ = std::make_unique<AsyncSpdmTcpIO>(std::move(*socket_), ioCtx_);

        auto status = co_await asyncInitConnection();
        if (LIBSPDM_STATUS_IS_ERROR(status))
        {
            LOG_ERROR(
                "AsyncSpdmRequester: init_connection failed: 0x{:x}",
                static_cast<uint32_t>(status));
            co_return false;
        }
        LOG_INFO("AsyncSpdmRequester: SPDM negotiation complete");
        co_return true;
    }

    /**
     * @brief Send a raw SPDM request asynchronously.
     *
     * Convenience wrapper for use after init_connection.
     */
    boost::asio::awaitable<libspdm_return_t> asyncSendRequest(
        const uint32_t* session_id, bool is_app_message, size_t request_size,
        void* request)
    {
        LIBSPDM_ASSERT(io_ != nullptr);
        co_return co_await libspdm_send_request_async(
            spdmContext, session_id, is_app_message, request_size, request,
            *io_);
    }

    /**
     * @brief Receive a raw SPDM response asynchronously.
     */
    boost::asio::awaitable<libspdm_return_t> asyncReceiveResponse(
        const uint32_t* session_id, bool is_app_message, size_t* response_size,
        void** response)
    {
        LIBSPDM_ASSERT(io_ != nullptr);
        co_return co_await libspdm_receive_response_async(
            spdmContext, session_id, is_app_message, response_size, response,
            *io_);
    }

    void* getSpdmContext()
    {
        return spdmContext;
    }

    /**
     * @brief Access the underlying async I/O object after connectAndInit().
     *
     * Used by AsyncComponentIntegrity to pass directly to the async SPDM
     * coroutine helpers (libspdm_get_digest_async, etc.).
     */
    AsyncSpdmTcpIO& getIO()
    {
        LIBSPDM_ASSERT(io_ != nullptr);
        return *io_;
    }

  private:
    // No-op device IO stubs — libspdm requires registered functions even when
    // the async path never calls them.  If they are ever reached it is a bug.
    // Signatures must match libspdm_device_send_message_func and
    // libspdm_device_receive_message_func from spdm_common_lib.h exactly.
    static libspdm_return_t noopSend(void*, size_t, const void*, uint64_t)
    {
        LIBSPDM_ASSERT(false); // should never be called in the async path
        return LIBSPDM_STATUS_SEND_FAIL;
    }
    static libspdm_return_t noopReceive(void*, size_t*, void**, uint64_t)
    {
        LIBSPDM_ASSERT(false); // should never be called in the async path
        return LIBSPDM_STATUS_RECEIVE_FAIL;
    }

    boost::asio::io_context& ioCtx_;
    tcp::resolver resolver_;
    std::shared_ptr<tcp::socket> socket_;
    std::unique_ptr<AsyncSpdmTcpIO> io_;
};
