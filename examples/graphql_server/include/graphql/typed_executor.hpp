#pragma once

#include "graphql/parser.hpp"
#include "graphql/typed_schema.hpp"
#include "graphql/util.hpp"

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace NSNAME::graphql
{

template <typename Provider>
class TypedExecutor
{
  public:
    TypedExecutor(TypedSchema schema, std::shared_ptr<Provider> provider) :
        schema(std::move(schema)),
        provider(std::move(provider))
    {}

    boost::asio::awaitable<nlohmann::json> execute(
        const std::string& query,
        const nlohmann::json& variables = nlohmann::json::object())
    {
        nlohmann::json response;

        try
        {
            std::string errorMsg;
            if (!Parser::validate(query, errorMsg))
            {
                response["errors"] =
                    nlohmann::json::array({{{"message", errorMsg}}});
                co_return response;
            }

            Operation operation = Parser::parse(query);
            schema.validateOperation(operation);

            nlohmann::json mergedVariables = initialVariables(operation);
            for (auto it = variables.begin(); it != variables.end(); ++it)
            {
                mergedVariables[it.key()] = it.value();
            }

            response["data"] =
                co_await executeSelections(operation.selections, mergedVariables);
        }
        catch (const std::exception& e)
        {
            response["errors"] =
                nlohmann::json::array({{{"message", e.what()}}});
        }

        co_return response;
    }

    // Execute a subscription: calls `onEvent` for each poll result until
    // `onEvent` returns false. Polls every `interval`.
    // onEvent is an async callback: awaitable<bool>(nlohmann::json)
    // returning false signals the subscriber wants to stop.
    template <typename AsyncEventFn>
    boost::asio::awaitable<void> executeSubscription(
        const std::string& query, const nlohmann::json& variables,
        std::chrono::steady_clock::duration interval, AsyncEventFn onEvent)
    {
        std::string errorMsg;
        if (!Parser::validate(query, errorMsg))
        {
            nlohmann::json err;
            err["errors"] = nlohmann::json::array({{{"message", errorMsg}}});
            co_await onEvent(std::move(err));
            co_return;
        }

        Operation operation = Parser::parse(query);
        schema.validateOperation(operation);

        nlohmann::json mergedVariables = initialVariables(operation);
        for (auto it = variables.begin(); it != variables.end(); ++it)
        {
            mergedVariables[it.key()] = it.value();
        }

        auto exec = co_await boost::asio::this_coro::executor;
        boost::asio::steady_timer timer(exec);

        while (true)
        {
            nlohmann::json event;
            try
            {
                event["data"] = co_await executeSubscriptionSelections(
                    operation.selections, mergedVariables);
            }
            catch (const std::exception& e)
            {
                event["errors"] =
                    nlohmann::json::array({{{"message", e.what()}}});
            }

            bool cont = co_await onEvent(std::move(event));
            if (!cont)
            {
                co_return;
            }

            timer.expires_after(interval);
            boost::system::error_code ec;
            co_await timer.async_wait(
                boost::asio::redirect_error(boost::asio::use_awaitable, ec));
            if (ec)
            {
                co_return; // cancelled
            }
        }
    }

  protected:
    virtual boost::asio::awaitable<nlohmann::json> resolveRootField(
        const FieldSelection& selection, const FieldSpec& fieldSpec,
        const nlohmann::json& variables) = 0;

    virtual boost::asio::awaitable<nlohmann::json> resolveSubscriptionField(
        const FieldSelection& selection, const FieldSpec& fieldSpec,
        const nlohmann::json& variables)
    {
        // Default: delegate to the same resolution as queries
        co_return co_await resolveRootField(selection, fieldSpec, variables);
    }

    boost::asio::awaitable<nlohmann::json> executeSubscriptionSelections(
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
                throw std::runtime_error("Unknown subscription field: " +
                                         selection.name);
            }

            std::string outputName =
                selection.alias.empty() ? selection.name : selection.alias;
            result[outputName] = co_await resolveSubscriptionField(
                selection, *fieldSpec, variables);
        }
        co_return result;
    }

    boost::asio::awaitable<nlohmann::json> executeSelections(
        const std::vector<FieldSelection>& selections,
        const nlohmann::json& variables)
    {
        nlohmann::json result = nlohmann::json::object();
        for (const FieldSelection& selection : selections)
        {
            const FieldSpec* fieldSpec = schema.getRootQueryField(selection.name);
            if (fieldSpec == nullptr)
            {
                throw std::runtime_error("Unknown query field: " + selection.name);
            }

            std::string outputName =
                selection.alias.empty() ? selection.name : selection.alias;
            result[outputName] =
                co_await resolveRootField(selection, *fieldSpec, variables);
        }
        co_return result;
    }

    boost::asio::awaitable<nlohmann::json> projectField(
        const FieldSelection& selection, const FieldSpec& fieldSpec,
        const nlohmann::json& source)
    {
        if (!source.contains(fieldSpec.responseKey))
        {
            co_return nullptr;
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
                throw std::runtime_error("Expected array for field '" +
                                         selection.name + "'");
            }

            nlohmann::json result = nlohmann::json::array();
            for (const auto& item : value)
            {
                result.push_back(co_await projectObject(item, fieldSpec.returnType,
                                                        selection.selections));
            }
            co_return result;
        }

        co_return co_await projectObject(value, fieldSpec.returnType,
                                         selection.selections);
    }

    boost::asio::awaitable<nlohmann::json> projectObject(
        const nlohmann::json& source, const std::string& typeName,
        const std::vector<FieldSelection>& selections)
    {
        const ObjectSpec* objectSpec = schema.getObject(typeName);
        if (objectSpec == nullptr)
        {
            throw std::runtime_error("Unknown object type: " + typeName);
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
                throw std::runtime_error("Unknown field '" + selection.name +
                                         "' on type '" + typeName + "'");
            }

            std::string outputName =
                selection.alias.empty() ? selection.name : selection.alias;
            result[outputName] =
                co_await projectField(selection, fieldIt->second, source);
        }

        co_return result;
    }

    nlohmann::json resolveArguments(const FieldSelection& selection,
                                    const nlohmann::json& variables) const
    {
        return argumentsToJson(selection.arguments, variables);
    }

    TypedSchema schema;
    std::shared_ptr<Provider> provider;
};

} // namespace NSNAME::graphql
