#pragma once
extern "C"
{
#include <hal/library/debuglib.h>
#include <hal/library/memlib.h>
#include <libspdm/include/library/spdm_common_lib.h>
#include <libspdm/include/library/spdm_transport_tcp_lib.h>
}
#include <cstring>
#define LIBSPDM_TRANSPORT_HEADER_SIZE 64
#define LIBSPDM_TRANSPORT_TAIL_SIZE 64

/* define common LIBSPDM_TRANSPORT_ADDITIONAL_SIZE. It should be the biggest
 * one. */
#define LIBSPDM_TRANSPORT_ADDITIONAL_SIZE                                      \
    (LIBSPDM_TRANSPORT_HEADER_SIZE + LIBSPDM_TRANSPORT_TAIL_SIZE)

#if LIBSPDM_TRANSPORT_ADDITIONAL_SIZE < LIBSPDM_NONE_TRANSPORT_ADDITIONAL_SIZE
#error LIBSPDM_TRANSPORT_ADDITIONAL_SIZE is smaller than the required size in NONE
#endif
#if LIBSPDM_TRANSPORT_ADDITIONAL_SIZE < LIBSPDM_TCP_TRANSPORT_ADDITIONAL_SIZE
#error LIBSPDM_TRANSPORT_ADDITIONAL_SIZE is smaller than the required size in TCP
#endif
#if LIBSPDM_TRANSPORT_ADDITIONAL_SIZE <                                        \
    LIBSPDM_PCI_DOE_TRANSPORT_ADDITIONAL_SIZE
#error LIBSPDM_TRANSPORT_ADDITIONAL_SIZE is smaller than the required size in PCI_DOE
#endif
#if LIBSPDM_TRANSPORT_ADDITIONAL_SIZE < LIBSPDM_MCTP_TRANSPORT_ADDITIONAL_SIZE
#error LIBSPDM_TRANSPORT_ADDITIONAL_SIZE is smaller than the required size in MCTP
#endif

#ifndef LIBSPDM_SENDER_BUFFER_SIZE
#define LIBSPDM_SENDER_BUFFER_SIZE (0x1000 + LIBSPDM_TRANSPORT_ADDITIONAL_SIZE)
#endif
#ifndef LIBSPDM_RECEIVER_BUFFER_SIZE
#define LIBSPDM_RECEIVER_BUFFER_SIZE                                           \
    (0x1100 + LIBSPDM_TRANSPORT_ADDITIONAL_SIZE)
#endif

/* Maximum size of a single SPDM message.
 * It matches DataTransferSize in SPDM specification. */
#define LIBSPDM_SENDER_DATA_TRANSFER_SIZE                                      \
    (LIBSPDM_SENDER_BUFFER_SIZE - LIBSPDM_TRANSPORT_ADDITIONAL_SIZE)
#define LIBSPDM_RECEIVER_DATA_TRANSFER_SIZE                                    \
    (LIBSPDM_RECEIVER_BUFFER_SIZE - LIBSPDM_TRANSPORT_ADDITIONAL_SIZE)
#define LIBSPDM_DATA_TRANSFER_SIZE LIBSPDM_RECEIVER_DATA_TRANSFER_SIZE

#if (LIBSPDM_SENDER_BUFFER_SIZE > LIBSPDM_RECEIVER_BUFFER_SIZE)
#define LIBSPDM_MAX_SENDER_RECEIVER_BUFFER_SIZE LIBSPDM_SENDER_BUFFER_SIZE
#else
#define LIBSPDM_MAX_SENDER_RECEIVER_BUFFER_SIZE LIBSPDM_RECEIVER_BUFFER_SIZE
#endif

#ifndef LIBSPDM_MAX_SPDM_MSG_SIZE
#define LIBSPDM_MAX_SPDM_MSG_SIZE 0x1200
#endif
static libspdm_return_t spdm_device_acquire_sender_buffer(void* context,
                                                          void** msg_buf_ptr);
static void spdm_device_release_sender_buffer(void* context,
                                              const void* msg_buf_ptr);
static libspdm_return_t spdm_device_acquire_receiver_buffer(void* context,
                                                            void** msg_buf_ptr);
static void spdm_device_release_receiver_buffer(void* context,
                                                const void* msg_buf_ptr);
// static bool clone_l1l2_with_sig(void* context,
//                                 libspdm_session_info_t* session_info,
//                                 const void* sig, size_t sig_size);
struct SpdmConnection
{
    struct AppContextData
    {
        SpdmConnection* spdmConnection{nullptr};
        AppContextData(SpdmConnection* conn) : spdmConnection(conn)
        {
            // Initialize other members as needed
        }
        std::array<uint8_t, LIBSPDM_MAX_SENDER_RECEIVER_BUFFER_SIZE> msgBuffer{
            0};
        bool msgBufferAcquired{false};
    };
    SpdmConnection() : appContextData(this)
    {
        contextSize = libspdm_get_context_size();
        spdmContext = malloc(contextSize);
        if (spdmContext == nullptr)
        {
            // Handle allocation failure
            throw std::runtime_error("Failed to allocate SPDM context");
        }
        std::memset(spdmContext, 0, contextSize);
        libspdm_init_context(spdmContext);

        libspdm_return_t status;

        status = libspdm_set_data(
            spdmContext, LIBSPDM_DATA_APP_CONTEXT_DATA,
            nullptr,         // Pointer to the data parameter
            &appContextData, // Pointer to your custom data
            sizeof(void*));  // Size of your custom data pointer

        if (LIBSPDM_STATUS_SUCCESS != status)
        {
            throw std::runtime_error("Failed to set custom context");
        }
        libspdm_register_transport_layer_func(
            spdmContext,
            LIBSPDM_MAX_SPDM_MSG_SIZE, // define as needed
            0, 0,                      // header/tail size for TCP
            libspdm_transport_tcp_encode_message,
            libspdm_transport_tcp_decode_message);

        libspdm_register_device_buffer_func(
            spdmContext, LIBSPDM_SENDER_BUFFER_SIZE,
            LIBSPDM_RECEIVER_BUFFER_SIZE, spdm_device_acquire_sender_buffer,
            spdm_device_release_sender_buffer,
            spdm_device_acquire_receiver_buffer,
            spdm_device_release_receiver_buffer);
    }
    SpdmConnection(const SpdmConnection&) = delete;
    SpdmConnection& operator=(const SpdmConnection&) = delete;
    SpdmConnection(SpdmConnection&&) = delete;
    SpdmConnection& operator=(SpdmConnection&&) = delete;
    void setRTTTimeout(void* spdm_context, uint8_t timeOut)
    {
        libspdm_return_t status;

        status = libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_RTT_US,
                                  nullptr, // Pointer to the data parameter
                                  &timeOut, sizeof(timeOut));

        if (LIBSPDM_STATUS_SUCCESS != status)
        {
            LOG_ERROR("Failed to set RTT timeout in SPDM context: %d\n",
                      status);
        }
    }
    static AppContextData* fromContext(void* spdm_context)
    {
        void* retrieved_data{nullptr};
        size_t data_size = sizeof(void*);

        // Get the size of the stored data first
        libspdm_return_t status =
            libspdm_get_data(spdm_context, LIBSPDM_DATA_APP_CONTEXT_DATA,
                             nullptr, &retrieved_data, &data_size);

        if (LIBSPDM_STATUS_SUCCESS != status)
        {
            // Handle error: Data not found, invalid context, etc.
            return nullptr;
        }

        return static_cast<AppContextData*>(retrieved_data);
    }
    ~SpdmConnection()
    {
        libspdm_deinit_context(spdmContext);
        std::free(spdmContext);
        std::free(scratchBuffer);
    }
    AppContextData appContextData;
    void* spdmContext{nullptr};
    void* scratchBuffer = nullptr;
    size_t contextSize = 0;
    size_t scratchSize = 0;
};
template <typename Derived>
struct SpdmConnectionTemplate : SpdmConnection
{
    SpdmConnectionTemplate()
    {
        libspdm_register_device_io_func(spdmContext,
                                        Derived::device_send_message,
                                        Derived::device_receive_message);
        scratchSize = libspdm_get_sizeof_required_scratch_buffer(spdmContext);
        scratchBuffer = std::malloc(scratchSize);
        libspdm_set_scratch_buffer(spdmContext, scratchBuffer, scratchSize);
    }
};

static libspdm_return_t spdm_device_acquire_sender_buffer(void* context,
                                                          void** msg_buf_ptr)
{
    SpdmConnection::AppContextData* ctx = SpdmConnection::fromContext(context);
    LIBSPDM_ASSERT(ctx != nullptr);
    LIBSPDM_ASSERT(!ctx->msgBufferAcquired);

    *msg_buf_ptr = ctx->msgBuffer.data();
    libspdm_zero_mem(ctx->msgBuffer.data(), ctx->msgBuffer.size());
    ctx->msgBufferAcquired = true;
    return LIBSPDM_STATUS_SUCCESS;
}

static void spdm_device_release_sender_buffer(void* context,
                                              const void* msg_buf_ptr)
{
    SpdmConnection::AppContextData* conn = SpdmConnection::fromContext(context);
    LIBSPDM_ASSERT(conn != NULL);
    LIBSPDM_ASSERT(conn->msgBufferAcquired);
    LIBSPDM_ASSERT(msg_buf_ptr == conn->msgBuffer.data());
    conn->msgBufferAcquired = false;
    return;
}

static libspdm_return_t spdm_device_acquire_receiver_buffer(void* context,
                                                            void** msg_buf_ptr)
{
    SpdmConnection::AppContextData* conn = SpdmConnection::fromContext(context);
    LIBSPDM_ASSERT(conn != NULL);
    LIBSPDM_ASSERT(!conn->msgBufferAcquired);

    *msg_buf_ptr = conn->msgBuffer.data();
    libspdm_zero_mem(conn->msgBuffer.data(), conn->msgBuffer.size());
    conn->msgBufferAcquired = true;
    return LIBSPDM_STATUS_SUCCESS;
}

static void spdm_device_release_receiver_buffer(void* context,
                                                const void* msg_buf_ptr)
{
    SpdmConnection::AppContextData* conn = SpdmConnection::fromContext(context);
    LIBSPDM_ASSERT(conn != NULL);
    LIBSPDM_ASSERT(conn->msgBufferAcquired);
    LIBSPDM_ASSERT(msg_buf_ptr == conn->msgBuffer.data());
    conn->msgBufferAcquired = false;
    return;
}

/* callback function provided by spdm requester to copy l1l2 log */
// static bool clone_l1l2_with_sig(void* context,
//                                 libspdm_session_info_t* session_info,
//                                 const void* sig, size_t sig_size)
// {
// bool result;
// libspdm_return_t status;
// result = libspdm_calculate_l1l2(
//     (libspdm_context_t*)context, session_info,
//     &((spdm_conn_t*)((libspdm_context_t*)context)->conn)->clone_l1l2);
// if (result == false)
// {
//     debug_log(LOG_ERR, "Error! Clone l1l2 failed!\n");
//     return false;
// }

// status = libspdm_append_managed_buffer(
//     &((spdm_conn_t*)((libspdm_context_t*)context)->conn)->clone_l1l2,
//     sig, sig_size);
// if (LIBSPDM_STATUS_IS_ERROR(status))
// {
//     debug_log(LOG_ERR, "Error! Append sigature to l1l2 failed with
//     0x%x!\n",
//               status);
//     return false;
// }

// return true;
// }
