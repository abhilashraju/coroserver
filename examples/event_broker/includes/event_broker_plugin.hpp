#pragma once

#include "eventqueue.hpp"

#include <memory>
#include <string>
#define PLUGIN_SYMBOL_EXPORT extern "C" __attribute__((visibility("default")))

class PluginIface : public std::enable_shared_from_this<PluginIface>
{
  public:
    virtual ~PluginIface() = 0; // Virtual destructor
    virtual bool hasInterface(const std::string& interfaceId) = 0;
    virtual std::shared_ptr<PluginIface>
        getInterface(const std::string& interfaceId) = 0;
    static const char* iid()
    {
        return "iid_PluginIface";
    }
    template <typename Iface>
    static inline std::shared_ptr<Iface> getInterface(PluginIface* baseIface)
    {
        if (baseIface->hasInterface(Iface::iid()))
        {
            return std::static_pointer_cast<Iface>(
                baseIface->getInterface(Iface::iid()));
        }
        return std::shared_ptr<Iface>();
    }
};
inline PluginIface::~PluginIface() = default;
inline std::shared_ptr<PluginIface>
    PluginIface::getInterface(const std::string& interfaceId)
{
    if (interfaceId == iid())
    {
        return this->shared_from_this();
    }
    return {};
}
inline bool PluginIface::hasInterface(const std::string& interfaceId)
{
    return interfaceId == iid();
}
struct EventBrokerPlugin : public PluginIface
{
    using Providers = std::map<std::string, EventQueue::EventProvider>;
    using Consumers = std::map<std::string, EventQueue::EventConsumer>;

  public:
    virtual Providers getProviders() = 0;   // Pure virtual function
    virtual Consumers getConsumers() = 0;   // Pure virtual function
    virtual ~EventBrokerPlugin() = default; // Virtual destructor
    static const char* iid()
    {
        return "iid_broker_plugin";
    }
};
