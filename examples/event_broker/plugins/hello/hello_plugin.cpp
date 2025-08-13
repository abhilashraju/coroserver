#include "event_broker_plugin.hpp"
using namespace NSNAME;
class MyBrokerPlugin : public virtual EventBrokerPlugin
{
  public:
    MyBrokerPlugin() {}
    EventBrokerPlugin::Consumers getConsumers() override
    {
        return {{"Hi",
                 [](Streamer, const std::string&)
                     -> net::awaitable<boost::system::error_code> {
                     LOG_INFO("Hi Consumer");
                     co_return boost::system::error_code{};
                 }}};
    }
    EventBrokerPlugin::Providers getProviders() override
    {
        return {{"Hi",
                 [](Streamer, const std::string&)
                     -> net::awaitable<boost::system::error_code> {
                     LOG_INFO("Hi Provider");
                     co_return boost::system::error_code{};
                 }}};
    }

    static std::shared_ptr<MyBrokerPlugin> create()
    {
        return std::make_shared<MyBrokerPlugin>();
    }
    bool hasInterface(const std::string& interfaceId) override
    {
        return interfaceId == EventBrokerPlugin::iid();
    }
    std::shared_ptr<PluginIface> getInterface(
        const std::string& interfaceId) override
    {
        if (interfaceId == EventBrokerPlugin::iid())
        {
            return this->shared_from_this();
        }
        return MyBrokerPlugin::getInterface(interfaceId);
    }
};
PLUGIN_SYMBOL_EXPORT std::shared_ptr<MyBrokerPlugin> create_object()
{
    return MyBrokerPlugin::create();
}
