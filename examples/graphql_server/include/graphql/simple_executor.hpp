#pragma once

#include "graphql/parser.hpp"
#include "graphql/simple_schema.hpp"
#include "graphql/util.hpp"

#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <stdexcept>
#include <string>

namespace NSNAME::graphql
{

class SimpleExecutor
{
  public:
    explicit SimpleExecutor(std::shared_ptr<SimpleSchema> schema) :
        schema_(std::move(schema))
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
            expandFragments(operation);
            nlohmann::json mergedVariables = initialVariables(operation);
            for (auto it = variables.begin(); it != variables.end(); ++it)
            {
                mergedVariables[it.key()] = it.value();
            }

            nlohmann::json data;
            switch (operation.type)
            {
                case Operation::Type::Query:
                    data = co_await executeQuery(operation, mergedVariables);
                    break;
                case Operation::Type::Mutation:
                    data = co_await executeMutation(operation, mergedVariables);
                    break;
                case Operation::Type::Subscription:
                    throw std::runtime_error(
                        "Subscriptions are not yet supported");
                default:
                    throw std::runtime_error("Unknown operation type");
            }

            response["data"] = data;
        }
        catch (const std::exception& e)
        {
            response["errors"] =
                nlohmann::json::array({{{"message", e.what()}}});
        }

        co_return response;
    }

  private:
    boost::asio::awaitable<nlohmann::json> executeQuery(
        const Operation& operation, const nlohmann::json& variables)
    {
        nlohmann::json result;

        for (const FieldSelection& field : operation.selections)
        {
            auto resolver = schema_->getQuery(field.name);
            if (!resolver)
            {
                throw std::runtime_error("Unknown query field: " + field.name);
            }

            nlohmann::json fieldArgs = variables;
            nlohmann::json parsedArgs = argumentsToJson(field.arguments, variables);
            for (auto it = parsedArgs.begin(); it != parsedArgs.end(); ++it)
            {
                fieldArgs[it.key()] = it.value();
            }

            nlohmann::json fieldResult =
                co_await (*resolver)(fieldArgs, nlohmann::json::object());
            fieldResult = filterFields(fieldResult, field.selections);

            std::string outputName =
                field.alias.empty() ? field.name : field.alias;
            result[outputName] = fieldResult;
        }

        co_return result;
    }

    boost::asio::awaitable<nlohmann::json> executeMutation(
        const Operation& operation, const nlohmann::json& variables)
    {
        nlohmann::json result;

        for (const FieldSelection& field : operation.selections)
        {
            auto resolver = schema_->getMutation(field.name);
            if (!resolver)
            {
                throw std::runtime_error("Unknown mutation field: " +
                                         field.name);
            }

            nlohmann::json fieldArgs = variables;
            nlohmann::json parsedArgs = argumentsToJson(field.arguments, variables);
            for (auto it = parsedArgs.begin(); it != parsedArgs.end(); ++it)
            {
                fieldArgs[it.key()] = it.value();
            }

            nlohmann::json fieldResult =
                co_await (*resolver)(fieldArgs, nlohmann::json::object());
            fieldResult = filterFields(fieldResult, field.selections);

            std::string outputName =
                field.alias.empty() ? field.name : field.alias;
            result[outputName] = fieldResult;
        }

        co_return result;
    }

    std::shared_ptr<SimpleSchema> schema_;
};

} // namespace NSNAME::graphql
