#include "event_broker_plugin.hpp"
class RsyncPlugin : public virtual EventBrokerPlugin
{
  public:
    RsyncPlugin() {}
    EventBrokerPlugin::Consumers getConsumers() override
    {
        return {
            {"FileModified", std::bind_front(&RsyncPlugin::fileConsumer, this)},
            {"ArchiveModified",
             std::bind_front(&RsyncPlugin::fileConsumer, this)},
            {"FileDeleted", std::bind_front(&RsyncPlugin::fileConsumer, this)}};
    }
    net::awaitable<boost::system::error_code> fileConsumer(
        Streamer streamer, const std::string& event) const
    {
        auto [id, data] = parseEvent(event);
        if (id == "FileModified" || id == "ArchiveModified")
        {
            co_return co_await recieveFile(streamer, data);
        }
        if (id == "FileDeleted")
        {
            co_return co_await deleteFile(data);
        }
        co_return boost::system::error_code{
            boost::system::errc::operation_not_supported,
            boost::system::system_category()};
    }
    EventBrokerPlugin::Providers getProviders() override
    {
        return {{"FileModified",
                 std::bind_front(&RsyncPlugin::providerHandler, this)},
                {"ArchiveModified",
                 std::bind_front(&RsyncPlugin::providerHandler, this)},
                {"FileDeleted",
                 std::bind_front(&RsyncPlugin::providerHandler, this)}};
    }
    net::awaitable<boost::system::error_code> providerHandler(
        Streamer streamer, const std::string& event) const
    {
        co_return boost::system::error_code{
            boost::system::errc::operation_not_supported,
            boost::system::system_category()};
    }
    static std::shared_ptr<RsyncPlugin> create()
    {
        return std::make_shared<RsyncPlugin>();
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
        return RsyncPlugin::getInterface(interfaceId);
    }
};
PLUGIN_SYMBOL_EXPORT std::shared_ptr<RsyncPlugin> create_object()
{
    return RsyncPlugin::create();
}
