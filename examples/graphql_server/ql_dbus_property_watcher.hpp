#pragma once

#include "dbusproperty_watcher.hpp"
#include "graphql/typed_schema.hpp"

#include <sdbusplus/asio/connection.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace NSNAME
{

// Reverse index: key = dbusObject + '\0' + dbusInterface + '\0' + dbusProperty
// Empty object or property component acts as a wildcard during lookup.
using DbusWatcherIndex =
    std::unordered_map<std::string, std::vector<std::string>>;

// Build the reverse index from the DbusWatcher table in the schema.
inline DbusWatcherIndex buildDbusWatcherIndex(
    const std::vector<graphql::DbusWatcher>& watchers)
{
    DbusWatcherIndex idx;
    for (const auto& w : watchers)
    {
        std::string key =
            w.dbusObject + '\0' + w.dbusInterface + '\0' + w.dbusProperty;
        for (const auto& field : w.graphqlFields)
        {
            idx[key].push_back(field);
        }
    }
    return idx;
}

// Resolve all GraphQL field names affected by a (object, interface, property)
// triple. Tries four specificity levels, from most to least specific:
//   1. object + interface + property
//   2. object + interface + ""        (any property on this iface for this
//   object)
//   3. ""     + interface + property  (any object, specific property)
//   4. ""     + interface + ""        (any object, any property on interface)
inline std::vector<std::string> resolveFields(
    const DbusWatcherIndex& index, const std::string& object,
    const std::string& iface, const std::string& property)
{
    std::vector<std::string> fields;
    auto collect = [&](const std::string& key) {
        auto it = index.find(key);
        if (it != index.end())
        {
            for (const auto& f : it->second)
            {
                fields.push_back(f);
            }
        }
    };
    collect(object + '\0' + iface + '\0' + property);
    collect(object + '\0' + iface + '\0');
    collect('\0' + iface + '\0' + property);
    collect('\0' + iface + '\0');
    return fields;
}

// GraphQLDbusPropertyWatcher: registers a single bus-wide PropertiesChanged
// match rule and filters every signal through the DbusWatcherIndex.
//
// Any signal whose (object, interface, property) triple resolves to one or
// more GraphQL fields triggers a single notifyFn_ call with the deduplicated
// set of affected fields. Signals that resolve to nothing are discarded with
// a single O(1) index miss — no per-object or per-interface rules needed.
//
// Usage:
//   auto watcher = std::make_shared<GraphQLDbusPropertyWatcher>(
//       conn, schema.getDbusWatchers(),
//       [executor](const std::unordered_set<std::string>& fields) {
//           executor->notifyFieldsChanged(fields);
//       });
class GraphQLDbusPropertyWatcher
{
  public:
    using NotifyFn = std::function<void(
        const std::unordered_set<std::string>& graphqlFields)>;

    GraphQLDbusPropertyWatcher(
        std::shared_ptr<sdbusplus::asio::connection> conn,
        const std::vector<graphql::DbusWatcher>& watchers, NotifyFn notifyFn) :
        conn_(conn), index_(buildDbusWatcherIndex(watchers)),
        notifyFn_(std::move(notifyFn))
    {
        // One bus-wide rule for all PropertiesChanged signals.
        // The index filters out unrelated traffic with a single map lookup.
        const std::string rule =
            sdbusplus::bus::match::rules::type::signal() +
            sdbusplus::bus::match::rules::member("PropertiesChanged") +
            sdbusplus::bus::match::rules::interface(
                "org.freedesktop.DBus.Properties");

        watcher_ = DbusSignalWatcher<sdbusplus::message_t>::create(conn_, rule);

        net::co_spawn(
            conn_->get_io_context(),
            watcher_->watch([this, watcher = watcher_](
                                const boost::system::error_code& ec,
                                std::optional<sdbusplus::message_t> maybeMsg)
                                -> net::awaitable<void> {
                if (ec || !maybeMsg)
                {
                    co_return;
                }

                sdbusplus::message_t msg = std::move(*maybeMsg);
                const std::string objectPath = msg.get_path();

                std::string interfaceName;
                PropertyMap changedProperties;
                std::vector<std::string> invalidatedProperties;

                try
                {
                    msg.read(interfaceName, changedProperties,
                             invalidatedProperties);
                }
                catch (const std::exception&)
                {
                    co_return; // malformed signal — skip
                }

                // Resolve all changed and invalidated property names to
                // GraphQL fields. Deduplicate so notifyFn_ is called once
                // per signal regardless of how many properties changed.
                std::unordered_set<std::string> affectedFields;
                for (const auto& [prop, value] : changedProperties)
                {
                    for (const auto& field :
                         resolveFields(index_, objectPath, interfaceName, prop))
                    {
                        affectedFields.insert(field);
                    }
                }
                for (const auto& prop : invalidatedProperties)
                {
                    for (const auto& field :
                         resolveFields(index_, objectPath, interfaceName, prop))
                    {
                        affectedFields.insert(field);
                    }
                }

                if (!affectedFields.empty())
                {
                    notifyFn_(affectedFields);
                }

                co_return;
            }),
            net::detached);
    }

  private:
    std::shared_ptr<sdbusplus::asio::connection> conn_;
    DbusWatcherIndex index_;
    NotifyFn notifyFn_;
    // Single watcher — must stay alive for the lifetime of this object.
    std::shared_ptr<DbusSignalWatcher<sdbusplus::message_t>> watcher_;
};

} // namespace NSNAME
