#pragma once
#include "name_space.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NSNAME
{

// GraphQL field resolver type
using FieldResolver = std::function<boost::asio::awaitable<nlohmann::json>(
    const nlohmann::json& args, const nlohmann::json& context)>;

// GraphQL type definition
struct GraphQLType
{
    std::string name;
    std::unordered_map<std::string, FieldResolver> fields;
};

// GraphQL schema
class GraphQLSchema
{
  public:
    void addType(const std::string& typeName, GraphQLType type)
    {
        types[typeName] = std::move(type);
    }

    void addQuery(const std::string& queryName, FieldResolver resolver)
    {
        queries[queryName] = std::move(resolver);
    }

    void addMutation(const std::string& mutationName, FieldResolver resolver)
    {
        mutations[mutationName] = std::move(resolver);
    }

    std::optional<FieldResolver> getQuery(const std::string& name) const
    {
        auto it = queries.find(name);
        if (it != queries.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    std::optional<FieldResolver> getMutation(const std::string& name) const
    {
        auto it = mutations.find(name);
        if (it != mutations.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    const GraphQLType* getType(const std::string& name) const
    {
        auto it = types.find(name);
        if (it != types.end())
        {
            return &it->second;
        }
        return nullptr;
    }

  private:
    std::unordered_map<std::string, GraphQLType> types;
    std::unordered_map<std::string, FieldResolver> queries;
    std::unordered_map<std::string, FieldResolver> mutations;
};

// Simple GraphQL query parser
class GraphQLParser
{
  public:
    struct ParsedQuery
    {
        std::string operationType; // "query" or "mutation"
        std::string operationName;
        std::vector<std::string> fields;
        nlohmann::json variables;
    };

    static ParsedQuery parse(const std::string& query)
    {
        ParsedQuery result;

        // Simple parser - in production, use a proper GraphQL parser library
        // This is a simplified version for demonstration

        // Determine operation type
        if (query.find("mutation") != std::string::npos)
        {
            result.operationType = "mutation";
        }
        else
        {
            result.operationType = "query";
        }

        // Extract operation name and fields
        size_t braceStart = query.find('{');
        size_t braceEnd = query.rfind('}');

        if (braceStart != std::string::npos && braceEnd != std::string::npos)
        {
            std::string fieldsStr =
                query.substr(braceStart + 1, braceEnd - braceStart - 1);

            // Simple field extraction (split by whitespace and newlines)
            std::istringstream iss(fieldsStr);
            std::string field;
            while (iss >> field)
            {
                // Remove parentheses and arguments for now
                size_t parenPos = field.find('(');
                if (parenPos != std::string::npos)
                {
                    field = field.substr(0, parenPos);
                }

                // Remove commas and braces
                field.erase(std::remove_if(field.begin(), field.end(),
                                           [](char c) {
                                               return c == ',' || c == '{' ||
                                                      c == '}';
                                           }),
                            field.end());

                if (!field.empty())
                {
                    result.fields.push_back(field);
                }
            }
        }

        return result;
    }

    static nlohmann::json parseArguments(const std::string& argsStr)
    {
        nlohmann::json args;

        // Simple argument parser
        // Format: (arg1: value1, arg2: value2)
        size_t start = argsStr.find('(');
        size_t end = argsStr.find(')');

        if (start != std::string::npos && end != std::string::npos)
        {
            std::string content = argsStr.substr(start + 1, end - start - 1);

            // Split by comma
            std::istringstream iss(content);
            std::string pair;
            while (std::getline(iss, pair, ','))
            {
                size_t colonPos = pair.find(':');
                if (colonPos != std::string::npos)
                {
                    std::string key = pair.substr(0, colonPos);
                    std::string value = pair.substr(colonPos + 1);

                    // Trim whitespace
                    key.erase(0, key.find_first_not_of(" \t\n\r"));
                    key.erase(key.find_last_not_of(" \t\n\r") + 1);
                    value.erase(0, value.find_first_not_of(" \t\n\r"));
                    value.erase(value.find_last_not_of(" \t\n\r") + 1);

                    // Remove quotes if present
                    if (!value.empty() && value.front() == '"' &&
                        value.back() == '"')
                    {
                        value = value.substr(1, value.length() - 2);
                    }

                    args[key] = value;
                }
            }
        }

        return args;
    }
};

// GraphQL executor
class GraphQLExecutor
{
  public:
    explicit GraphQLExecutor(std::shared_ptr<GraphQLSchema> schema) :
        schema_(std::move(schema))
    {}

    boost::asio::awaitable<nlohmann::json> execute(
        const std::string& query,
        const nlohmann::json& variables = nlohmann::json::object())
    {
        nlohmann::json response;

        try
        {
            auto parsed = GraphQLParser::parse(query);
            nlohmann::json data;

            if (parsed.operationType == "query")
            {
                data = co_await executeQuery(parsed, variables);
            }
            else if (parsed.operationType == "mutation")
            {
                data = co_await executeMutation(parsed, variables);
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
        const GraphQLParser::ParsedQuery& parsed,
        const nlohmann::json& variables)
    {
        nlohmann::json result;

        for (const auto& field : parsed.fields)
        {
            auto resolver = schema_->getQuery(field);
            if (resolver)
            {
                result[field] =
                    co_await (*resolver)(variables, nlohmann::json::object());
            }
            else
            {
                throw std::runtime_error("Unknown query field: " + field);
            }
        }

        co_return result;
    }

    boost::asio::awaitable<nlohmann::json> executeMutation(
        const GraphQLParser::ParsedQuery& parsed,
        const nlohmann::json& variables)
    {
        nlohmann::json result;

        for (const auto& field : parsed.fields)
        {
            auto resolver = schema_->getMutation(field);
            if (resolver)
            {
                result[field] =
                    co_await (*resolver)(variables, nlohmann::json::object());
            }
            else
            {
                throw std::runtime_error("Unknown mutation field: " + field);
            }
        }

        co_return result;
    }

    std::shared_ptr<GraphQLSchema> schema_;
};

} // namespace NSNAME

// Made with Bob
