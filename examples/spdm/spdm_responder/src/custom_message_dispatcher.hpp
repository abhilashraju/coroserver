#pragma once
#include "custom_message_handlers.hpp"
#include "logger.hpp"
#include "spdm_custom_messages.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>
extern "C"
{
#include <library/spdm_responder_lib.h>
}

/**
 * @brief Handles custom SPDM message dispatching and routing
 *
 * This class is responsible for managing custom message handlers,
 * routing messages to appropriate handlers, and sending responses.
 */
class CustomMessageDispatcher
{
  public:
    using SendCallback = std::function<bool(const void*, size_t)>;

    CustomMessageDispatcher() = default;

    void setSendCallback(SendCallback callback)
    {
        sendCallback = callback;
    }

    void setCustomMessageHandlers(
        const std::map<uint8_t, CustomMessageHandler>& handlers)
    {
        std::lock_guard<std::mutex> lock(handlersMutex_);
        customMessageHandlers_ = handlers;
    }

    void addCustomMessageHandler(uint8_t messageCode,
                                 CustomMessageHandler handler)
    {
        std::lock_guard<std::mutex> lock(handlersMutex_);
        customMessageHandlers_[messageCode] = handler;
    }

    /**
     * @brief Dispatch a decoded request to its custom handler and return the
     *        response bytes.  The caller is responsible for sending the response
     *        (e.g. by writing it into libspdm's response buffer).
     *
     * @return Non-empty vector on success; empty on unknown opcode or error.
     */
    std::vector<uint8_t> dispatchToHandler(
        const std::vector<uint8_t>& messageData)
    {
        if (messageData.size() < sizeof(SpdmMessageHeader) ||
            messageData.size() > MAX_CERTIFICATE_SIZE)
            return {};

        const auto* header =
            reinterpret_cast<const SpdmMessageHeader*>(messageData.data());

        CustomMessageHandler handler;
        {
            std::lock_guard<std::mutex> lock(handlersMutex_);
            auto it =
                customMessageHandlers_.find(header->request_response_code);
            if (it == customMessageHandlers_.end())
                return {};
            handler = it->second;
        }

        return handler(messageData);
    }

    /** Legacy: dispatch and immediately send via the registered callback. */
    bool handleCustomMessage(void* /*spdmContext*/,
                             const std::vector<uint8_t>& messageData)
    {
        std::vector<uint8_t> response = dispatchToHandler(messageData);
        if (response.empty())
            return false;
        if (sendCallback)
            return sendCallback(response.data(), response.size());
        return true;
    }

    bool hasHandler(uint8_t messageCode) const
    {
        std::lock_guard<std::mutex> lock(handlersMutex_);
        return customMessageHandlers_.find(messageCode) !=
               customMessageHandlers_.end();
    }

  private:
    std::map<uint8_t, CustomMessageHandler> customMessageHandlers_;
    mutable std::mutex handlersMutex_;
    SendCallback sendCallback;
};
