#pragma once
/**
 * Truly async port of libspdm_init_connection().
 *
 * Instead of calling the blocking C entry point and bridging its synchronous
 * callback model through detached threads, this header directly awaits local
 * coroutine ports of the three requester handshake stages used by upstream:
 *   - libspdm_get_version
 *   - libspdm_get_capabilities
 *   - libspdm_negotiate_algorithms
 *
 * This keeps the io_context thread fully non-blocking and removes the shim
 * callback / promise bridge entirely.
 */

#include "libspdm_async_io.hpp"
#include "libspdm_req_get_capabilities_async.hpp"
#include "libspdm_req_get_version_async.hpp"
#include "libspdm_req_negotiate_algorithms_async.hpp"

#include <boost/asio/awaitable.hpp>

extern "C"
{
#include <internal/libspdm_common_lib.h>
}

/**
 * @brief Async version of libspdm_init_connection().
 *
 * Mirrors upstream sequencing from libspdm_req_communication.c:
 *   GET_VERSION -> GET_CAPABILITIES -> NEGOTIATE_ALGORITHMS
 */
template <AsyncSpdmIO IO>
boost::asio::awaitable<libspdm_return_t> libspdm_init_connection_async(
    void* spdm_context, bool get_version_only, boost::asio::io_context&, IO& io)
{
    LOG_DEBUG("libspdm_init_connection_async: enter"
              " spdm_context={} get_version_only={}",
              spdm_context, get_version_only);

    if (spdm_context == nullptr)
    {
        LOG_ERROR("libspdm_init_connection_async: spdm_context is null — aborting");
        co_return LIBSPDM_STATUS_INVALID_PARAMETER;
    }

    /* ── Step 1: GET_VERSION ───────────────────────────────────────────── */
    LOG_DEBUG("libspdm_init_connection_async: step 1 — GET_VERSION");
    libspdm_return_t status =
        co_await libspdm_get_version_async(spdm_context, io);
    LOG_DEBUG("libspdm_init_connection_async: GET_VERSION returned 0x{:x}", status);
    if (LIBSPDM_STATUS_IS_ERROR(status))
    {
        LOG_ERROR("libspdm_init_connection_async: GET_VERSION failed, status=0x{:x}",
                  status);
        co_return status;
    }

    if (!get_version_only)
    {
        /* ── Step 2: GET_CAPABILITIES ──────────────────────────────────── */
        LOG_DEBUG("libspdm_init_connection_async: step 2 — GET_CAPABILITIES");
        status = co_await libspdm_get_capabilities_async(spdm_context, io);
        LOG_DEBUG("libspdm_init_connection_async: GET_CAPABILITIES returned 0x{:x}",
                  status);
        if (LIBSPDM_STATUS_IS_ERROR(status))
        {
            LOG_ERROR("libspdm_init_connection_async: GET_CAPABILITIES failed,"
                      " status=0x{:x}", status);
            co_return status;
        }

        /* ── Step 3: NEGOTIATE_ALGORITHMS ──────────────────────────────── */
        LOG_DEBUG("libspdm_init_connection_async: step 3 — NEGOTIATE_ALGORITHMS");
        status = co_await libspdm_negotiate_algorithms_async(spdm_context, io);
        LOG_DEBUG("libspdm_init_connection_async: NEGOTIATE_ALGORITHMS returned"
                  " 0x{:x}", status);
        if (LIBSPDM_STATUS_IS_ERROR(status))
        {
            LOG_ERROR("libspdm_init_connection_async: NEGOTIATE_ALGORITHMS failed,"
                      " status=0x{:x}", status);
            co_return status;
        }
    }
    else
    {
        LOG_DEBUG("libspdm_init_connection_async:"
                  " get_version_only=true, skipping capabilities and algorithms");
    }

    LOG_DEBUG("libspdm_init_connection_async: complete — all steps succeeded");
    co_return LIBSPDM_STATUS_SUCCESS;
}
