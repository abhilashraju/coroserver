#pragma once

#include "graphql/error.hpp"
#include "graphql/parser.hpp"
#include "graphql/typed_schema.hpp"
#include "graphql/util.hpp"

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace NSNAME::graphql
{

template <typename Provider>
class TypedExecutor : public std::enable_shared_from_this<TypedExecutor<Provider>>
{
  public:
    TypedExecutor(TypedSchema schema, std::shared_ptr<Provider> provider) :
        schema(std::move(schema)), provider(std::move(provider))
    {}

    nlohmann::json getSubscriptionStats() const
    {
        size_t activeSharedLoops = activeSubscriptions.size();
        size_t totalClientSubscribers = 0;
        nlohmann::json details = nlohmann::json::array();

        for (const auto& [key, session] : activeSubscriptions)
        {
            size_t subsCount = session->subscribers.size();
            totalClientSubscribers += subsCount;

            nlohmann::json sessionDetails = {
                {"key", key},
                {"subscriber_count", subsCount},
                {"last_result", session->lastResult ? *session->lastResult : nullptr}
            };
            details.push_back(sessionDetails);
        }

        size_t multiplexedSubscribers = 0;
        if (totalClientSubscribers > activeSharedLoops)
        {
            multiplexedSubscribers = totalClientSubscribers - activeSharedLoops;
        }

        return {
            {"active_shared_loops", activeSharedLoops},
            {"total_client_subscribers", totalClientSubscribers},
            {"multiplexed_subscribers", multiplexedSubscribers},
            {"subscriptions", details}
        };
    }

    // Public entry point for query execution.
    // Always returns a well-formed GraphQL response JSON object;
    // errors are embedded in {"errors":[...]} — no exceptions escape.
    boost::asio::awaitable<nlohmann::json> execute(
        const std::string& query,
        const nlohmann::json& variables = nlohmann::json::object())
    {
        nlohmann::json response;

        Result<Operation> parseResult = Parser::tryParse(query);
        if (!parseResult)
        {
            response["errors"] = nlohmann::json::array();
            response["errors"].push_back({{"message", parseResult.error()}});
            co_return response;
        }

        Operation operation = std::move(*parseResult);
        expandFragments(operation);

        if (auto r = schema.validateOperation(operation); !r)
        {
            response["errors"] = nlohmann::json::array();
            response["errors"].push_back({{"message", r.error()}});
            co_return response;
        }

        nlohmann::json mergedVariables = initialVariables(operation);
        for (auto it = variables.begin(); it != variables.end(); ++it)
        {
            mergedVariables[it.key()] = it.value();
        }

        Result<nlohmann::json> dataResult =
            co_await executeSelections(operation.selections, mergedVariables);
        if (!dataResult)
        {
            response["errors"] = nlohmann::json::array();
            response["errors"].push_back({{"message", dataResult.error()}});
        }
        else
        {
            response["data"] = std::move(*dataResult);
        }

        co_return response;
    }

    struct Subscriber
    {
        uint64_t id;
        std::function<boost::asio::awaitable<bool>(nlohmann::json)> callback;
        std::shared_ptr<boost::asio::steady_timer> disconnectTimer;
    };

    struct SubscriptionSession
    {
        std::string key;
        std::shared_ptr<boost::asio::steady_timer> timer;
        std::vector<Subscriber> subscribers;
        uint64_t nextSubscriberId{1};
        std::optional<nlohmann::json> lastResult;
        bool active{true};
    };

    struct SubscriberCleanupGuard
    {
        TypedExecutor& executor;
        std::weak_ptr<SubscriptionSession> sessionWeak;
        uint64_t subId;

        ~SubscriberCleanupGuard()
        {
            if (auto session = sessionWeak.lock())
            {
                executor.removeSubscriber(session, subId);
                if (session->subscribers.empty())
                {
                    session->timer->cancel();
                }
            }
        }
    };

    void removeSubscriber(std::shared_ptr<SubscriptionSession> session, uint64_t id)
    {
        auto it = std::find_if(session->subscribers.begin(), session->subscribers.end(),
                               [id](const Subscriber& s) { return s.id == id; });
        if (it != session->subscribers.end())
        {
            it->disconnectTimer->cancel();
            session->subscribers.erase(it);
        }
    }

    boost::asio::awaitable<void> runSubscriptionLoop(
        std::shared_ptr<SubscriptionSession> session,
        Operation operation,
        nlohmann::json mergedVariables,
        std::chrono::steady_clock::duration interval)
    {
        auto self = this->shared_from_this(); // keep executor alive

        while (session->active && !session->subscribers.empty())
        {
            Result<nlohmann::json> tickResult =
                co_await executeSubscriptionSelections(operation.selections,
                                                       mergedVariables);

            nlohmann::json event;
            if (!tickResult)
            {
                event["errors"] = nlohmann::json::array();
                event["errors"].push_back({{"message", tickResult.error()}});
            }
            else
            {
                event["data"] = std::move(*tickResult);
            }

            session->lastResult = event;

            // Broadcast to all active subscribers
            std::vector<uint64_t> failedSubIds;
            for (auto& sub : session->subscribers)
            {
                bool ok = co_await sub.callback(event);
                if (!ok)
                {
                    failedSubIds.push_back(sub.id);
                }
            }

            // Clean up disconnected subscribers
            for (uint64_t id : failedSubIds)
            {
                removeSubscriber(session, id);
            }

            if (session->subscribers.empty())
            {
                break;
            }

            session->timer->expires_after(interval);
            boost::system::error_code ec;
            co_await session->timer->async_wait(
                boost::asio::redirect_error(boost::asio::use_awaitable, ec));
            if (ec)
            {
                break;
            }
        }

        activeSubscriptions.erase(session->key);
    }

    // Execute a subscription: identical query/variables/interval requests
    // are coalesced into a single background polling loop.
    template <typename AsyncEventFn>
    boost::asio::awaitable<void> executeSubscription(
        const std::string& query, const nlohmann::json& variables,
        std::chrono::steady_clock::duration interval, AsyncEventFn onEvent)
    {
        Result<Operation> parseResult = Parser::tryParse(query);
        if (!parseResult)
        {
            nlohmann::json err;
            err["errors"] = nlohmann::json::array();
            err["errors"].push_back({{"message", parseResult.error()}});
            co_await onEvent(std::move(err));
            co_return;
        }

        Operation operation = std::move(*parseResult);
        expandFragments(operation);

        if (auto r = schema.validateOperation(operation); !r)
        {
            nlohmann::json err;
            err["errors"] = nlohmann::json::array();
            err["errors"].push_back({{"message", r.error()}});
            co_await onEvent(std::move(err));
            co_return;
        }

        nlohmann::json mergedVariables = initialVariables(operation);
        for (auto it = variables.begin(); it != variables.end(); ++it)
        {
            mergedVariables[it.key()] = it.value();
        }

        std::string key = query + "|" + mergedVariables.dump() + "|" + std::to_string(interval.count());

        auto exec = co_await boost::asio::this_coro::executor;

        auto it = activeSubscriptions.find(key);
        std::shared_ptr<SubscriptionSession> session;
        bool isNewSession = false;

        if (it == activeSubscriptions.end())
        {
            session = std::make_shared<SubscriptionSession>();
            session->key = key;
            session->timer = std::make_shared<boost::asio::steady_timer>(exec);
            activeSubscriptions[key] = session;
            isNewSession = true;
        }
        else
        {
            session = it->second;
        }

        auto disconnectTimer = std::make_shared<boost::asio::steady_timer>(exec);
        disconnectTimer->expires_at(std::chrono::steady_clock::time_point::max());

        uint64_t subId = session->nextSubscriberId++;
        Subscriber subscriber{
            subId,
            [onEvent](nlohmann::json event) -> boost::asio::awaitable<bool> {
                co_return co_await onEvent(std::move(event));
            },
            disconnectTimer
        };

        session->subscribers.push_back(std::move(subscriber));

        SubscriberCleanupGuard guard{*this, session, subId};

        if (isNewSession)
        {
            boost::asio::co_spawn(
                exec,
                runSubscriptionLoop(session, std::move(operation), mergedVariables, interval),
                boost::asio::detached);
        }
        else if (session->lastResult)
        {
            boost::asio::co_spawn(
                exec,
                [callback = session->subscribers.back().callback, lastResult = *session->lastResult, disconnectTimer]() -> boost::asio::awaitable<void> {
                    bool ok = co_await callback(lastResult);
                    if (!ok)
                    {
                        disconnectTimer->cancel();
                    }
                },
                boost::asio::detached);
        }

        boost::system::error_code ec;
        co_await disconnectTimer->async_wait(
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));

        co_return;
    }

  protected:
    virtual boost::asio::awaitable<Result<nlohmann::json>> resolveRootField(
        const FieldSelection& selection, const FieldSpec& fieldSpec,
        const nlohmann::json& variables) = 0;

    virtual boost::asio::awaitable<Result<nlohmann::json>>
        resolveSubscriptionField(const FieldSelection& selection,
                                 const FieldSpec& fieldSpec,
                                 const nlohmann::json& variables)
    {
        // Default: delegate to the same resolution as queries
        co_return co_await resolveRootField(selection, fieldSpec, variables);
    }

    boost::asio::awaitable<Result<nlohmann::json>> executeSubscriptionSelections(
        const std::vector<FieldSelection>& selections,
        const nlohmann::json& variables)
    {
        nlohmann::json result = nlohmann::json::object();
        for (const FieldSelection& selection : selections)
        {
            const FieldSpec* fieldSpec =
                schema.getRootSubscriptionField(selection.name);
            if (fieldSpec == nullptr)
            {
                co_return std::unexpected("Unknown subscription field: " +
                                          selection.name);
            }

            std::string outputName =
                selection.alias.empty() ? selection.name : selection.alias;
            Result<nlohmann::json> fieldResult =
                co_await resolveSubscriptionField(selection, *fieldSpec,
                                                  variables);
            if (!fieldResult)
            {
                co_return std::unexpected(fieldResult.error());
            }
            result[outputName] = std::move(*fieldResult);
        }
        co_return result;
    }

    boost::asio::awaitable<Result<nlohmann::json>> executeSelections(
        const std::vector<FieldSelection>& selections,
        const nlohmann::json& variables)
    {
        nlohmann::json result = nlohmann::json::object();
        for (const FieldSelection& selection : selections)
        {
            const FieldSpec* fieldSpec =
                schema.getRootQueryField(selection.name);
            if (fieldSpec == nullptr)
            {
                co_return std::unexpected("Unknown query field: " +
                                          selection.name);
            }

            std::string outputName =
                selection.alias.empty() ? selection.name : selection.alias;
            Result<nlohmann::json> fieldResult =
                co_await resolveRootField(selection, *fieldSpec, variables);
            if (!fieldResult)
            {
                co_return std::unexpected(fieldResult.error());
            }
            result[outputName] = std::move(*fieldResult);
        }
        co_return result;
    }

    boost::asio::awaitable<Result<nlohmann::json>> projectField(
        const FieldSelection& selection, const FieldSpec& fieldSpec,
        const nlohmann::json& source)
    {
        if (!source.contains(fieldSpec.responseKey))
        {
            co_return nlohmann::json(nullptr);
        }

        const nlohmann::json& value = source[fieldSpec.responseKey];
        if (fieldSpec.scalar)
        {
            co_return value;
        }

        if (fieldSpec.isList)
        {
            if (!value.is_array())
            {
                co_return std::unexpected("Expected array for field '" +
                                          selection.name + "'");
            }

            nlohmann::json result = nlohmann::json::array();
            for (const auto& item : value)
            {
                Result<nlohmann::json> itemResult = co_await projectObject(
                    item, fieldSpec.returnType, selection.selections);
                if (!itemResult)
                {
                    co_return std::unexpected(itemResult.error());
                }
                result.push_back(std::move(*itemResult));
            }
            co_return result;
        }

        co_return co_await projectObject(value, fieldSpec.returnType,
                                         selection.selections);
    }

    boost::asio::awaitable<Result<nlohmann::json>> projectObject(
        const nlohmann::json& source, const std::string& typeName,
        const std::vector<FieldSelection>& selections)
    {
        const ObjectSpec* objectSpec = schema.getObject(typeName);
        if (objectSpec == nullptr)
        {
            co_return std::unexpected("Unknown object type: " + typeName);
        }

        if (selections.empty())
        {
            co_return source;
        }

        nlohmann::json result = nlohmann::json::object();
        for (const FieldSelection& selection : selections)
        {
            auto fieldIt = objectSpec->fields.find(selection.name);
            if (fieldIt == objectSpec->fields.end())
            {
                co_return std::unexpected("Unknown field '" + selection.name +
                                          "' on type '" + typeName + "'");
            }

            std::string outputName =
                selection.alias.empty() ? selection.name : selection.alias;
            Result<nlohmann::json> fieldResult =
                co_await projectField(selection, fieldIt->second, source);
            if (!fieldResult)
            {
                co_return std::unexpected(fieldResult.error());
            }
            result[outputName] = std::move(*fieldResult);
        }

        co_return result;
    }

    nlohmann::json resolveArguments(const FieldSelection& selection,
                                    const nlohmann::json& variables) const
    {
        return argumentsToJson(selection.arguments, variables);
    }

    // Expand a redfishPath template by substituting {argName} placeholders.
    // Defaults from the FieldSpec argument list are applied first so that
    // optional arguments (e.g. systemId="system") are resolved even when the
    // caller omits them from the query string.
    // Example: expandPath("/redfish/v1/Systems/{systemId}/PCIeDevices",
    //                     {}, fieldSpec)  →  "/redfish/v1/Systems/system/PCIeDevices"
    static std::string expandPath(const std::string& pathTemplate,
                                  const nlohmann::json& args,
                                  const FieldSpec& fieldSpec)
    {
        // Seed with schema-level defaults, then overlay caller-supplied values.
        nlohmann::json resolved = nlohmann::json::object();
        for (const ArgumentSpec& argSpec : fieldSpec.arguments)
        {
            if (!argSpec.defaultValue.empty())
            {
                resolved[argSpec.name] = argSpec.defaultValue;
            }
        }
        for (auto it = args.begin(); it != args.end(); ++it)
        {
            if (!it.value().is_null())
            {
                resolved[it.key()] = it.value();
            }
        }

        std::string result = pathTemplate;
        for (auto it = resolved.begin(); it != resolved.end(); ++it)
        {
            const std::string placeholder = "{" + it.key() + "}";
            const std::string value = it.value().get<std::string>();
            std::string::size_type pos = 0;
            while ((pos = result.find(placeholder, pos)) != std::string::npos)
            {
                result.replace(pos, placeholder.size(), value);
                pos += value.size();
            }
        }
        return result;
    }

    // Generic resolution driven by FieldSpec::redfishPath.
    // Subclasses can call this when the field has a redfishPath set, or
    // override resolveRootField entirely and handle only their custom cases.
    boost::asio::awaitable<Result<nlohmann::json>> resolveByPath(
        const FieldSelection& selection, const FieldSpec& fieldSpec,
        const nlohmann::json& variables, bool fresh = false)
    {
        if (fieldSpec.redfishPath.empty())
        {
            co_return std::unexpected("No redfishPath defined for field: " +
                                      fieldSpec.name);
        }

        nlohmann::json args = resolveArguments(selection, variables);
        const std::string target = expandPath(fieldSpec.redfishPath, args,
                                              fieldSpec);

        Result<nlohmann::json> payloadResult =
            fresh ? co_await provider->getFresh(target)
                  : co_await provider->get(target);
        if (!payloadResult)
        {
            co_return std::unexpected(payloadResult.error());
        }
        nlohmann::json payload = std::move(*payloadResult);

        if (fieldSpec.isList)
        {
            if (!payload.contains("Members") || !payload["Members"].is_array())
            {
                co_return std::unexpected(
                    "Expected collection Members array for '" + fieldSpec.name +
                    "'");
            }

            nlohmann::json result = nlohmann::json::array();
            for (const auto& member : payload["Members"])
            {
                if (!member.contains("@odata.id"))
                {
                    continue;
                }
                Result<nlohmann::json> itemResult =
                    fresh ? co_await provider->getFresh(
                                member["@odata.id"].get<std::string>())
                          : co_await provider->get(
                                member["@odata.id"].get<std::string>());
                if (!itemResult)
                {
                    co_return std::unexpected(itemResult.error());
                }
                Result<nlohmann::json> projResult = co_await projectObject(
                    *itemResult, fieldSpec.returnType, selection.selections);
                if (!projResult)
                {
                    co_return std::unexpected(projResult.error());
                }
                result.push_back(std::move(*projResult));
            }
            co_return result;
        }

        if (fieldSpec.scalar)
        {
            co_return payload;
        }

        co_return co_await projectObject(payload, fieldSpec.returnType,
                                         selection.selections);
    }

    TypedSchema schema;
    std::shared_ptr<Provider> provider;

    std::unordered_map<std::string, std::shared_ptr<SubscriptionSession>> activeSubscriptions;
};

} // namespace NSNAME::graphql
