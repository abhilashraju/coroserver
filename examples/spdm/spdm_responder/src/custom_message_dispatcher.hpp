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

    bool handleCustomMessage(void* spdmContext,
                             const std::vector<uint8_t>& messageData)
    {
        if (messageData.empty())
        {
            return false;
        }

        // Check if it's a custom certificate exchange message
        if (messageData.size() < sizeof(SpdmMessageHeader))
        {
            return false;
        }

        // Validate message size against protocol-defined limits
        if (messageData.size() > MAX_CERTIFICATE_SIZE)
        {
            return false;
        }

        const auto* header =
            reinterpret_cast<const SpdmMessageHeader*>(messageData.data());

        // Look up handler in map with mutex protection
        CustomMessageHandler handler;
        {
            std::lock_guard<std::mutex> lock(handlersMutex_);
            auto it =
                customMessageHandlers_.find(header->request_response_code);
            if (it == customMessageHandlers_.end())
            {
                return false; // No handler registered for this message
            }
            handler = it->second;
        }

        // Call the handler outside the lock
        std::vector<uint8_t> response = handler(messageData);

        // Send response
        if (!response.empty() && sendCallback)
        {
            return sendCallback(response.data(), response.size());
        }

        return !response.empty();
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
