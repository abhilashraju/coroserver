#include "graphql_parser_libgraphql.hpp"

#include "graphql_handler.hpp"

namespace NSNAME
{

// Helper function to resolve variable references in arguments
static nlohmann::json resolveVariables(const nlohmann::json& value,
                                       const nlohmann::json& variables)
{
    if (value.is_object())
    {
        // Check if this is a variable reference marker
        if (value.contains("$variable") && value.size() == 1)
        {
            const std::string& varName = value["$variable"].get<std::string>();
            if (variables.contains(varName))
            {
                return variables[varName];
            }
            // Variable not found, return null
            return nullptr;
        }

        // Recursively resolve variables in object fields
        nlohmann::json result = nlohmann::json::object();
        for (auto it = value.begin(); it != value.end(); ++it)
        {
            result[it.key()] = resolveVariables(it.value(), variables);
        }
        return result;
    }
    else if (value.is_array())
    {
        // Recursively resolve variables in array elements
        nlohmann::json result = nlohmann::json::array();
        for (const auto& item : value)
        {
            result.push_back(resolveVariables(item, variables));
        }
        return result;
    }

    // Return value as-is for primitives
    return value;
}

// Helper function to filter JSON fields based on selection
static nlohmann::json filterFields(
    const nlohmann::json& data, const std::vector<std::string>& selectedFields)
{
    if (data.is_array())
    {
        nlohmann::json result = nlohmann::json::array();
        for (const auto& item : data)
        {
            result.push_back(filterFields(item, selectedFields));
        }
        return result;
    }
    else if (data.is_object())
    {
        nlohmann::json result = nlohmann::json::object();
        for (const auto& fieldName : selectedFields)
        {
            if (data.contains(fieldName))
            {
                result[fieldName] = data[fieldName];
            }
        }
        return result;
    }
    return data;
}

boost::asio::awaitable<nlohmann::json> LibGraphQLExecutor::execute(
    const std::string& query, const nlohmann::json& variables)
{
    nlohmann::json response;

    try
    {
        // Validate query first
        std::string errorMsg;
        if (!LibGraphQLParser::validate(query, errorMsg))
        {
            response["errors"] =
                nlohmann::json::array({{{"message", errorMsg}}});
            co_return response;
        }

        // Parse the query
        auto operation = LibGraphQLParser::parse(query);

        // Merge provided variables with operation variables
        nlohmann::json mergedVariables = operation.variables;
        for (auto it = variables.begin(); it != variables.end(); ++it)
        {
            mergedVariables[it.key()] = it.value();
        }

        nlohmann::json data;

        // Execute based on operation type
        switch (operation.type)
        {
            case ParsedOperation::OperationType::Query:
                data = co_await executeQuery(operation, mergedVariables);
                break;
            case ParsedOperation::OperationType::Mutation:
                data = co_await executeMutation(operation, mergedVariables);
                break;
            case ParsedOperation::OperationType::Subscription:
                throw std::runtime_error("Subscriptions are not yet supported");
            default:
                throw std::runtime_error("Unknown operation type");
        }

        response["data"] = data;
    }
    catch (const std::exception& e)
    {
        response["errors"] = nlohmann::json::array({{{"message", e.what()}}});
    }

    co_return response;
}

boost::asio::awaitable<nlohmann::json> LibGraphQLExecutor::executeQuery(
    const ParsedOperation& operation, const nlohmann::json& variables)
{
    nlohmann::json result;

    for (const auto& field : operation.selectionSet)
    {
        auto resolver = schema_->getQuery(field);
        if (resolver)
        {
            // Get field-specific arguments if available
            nlohmann::json fieldArgs = variables;
            auto argIt = operation.fieldArguments.find(field);
            if (argIt != operation.fieldArguments.end())
            {
                // Merge field arguments with variables, resolving variable
                // references
                for (auto it = argIt->second.begin(); it != argIt->second.end();
                     ++it)
                {
                    fieldArgs[it.key()] =
                        resolveVariables(it.value(), variables);
                }
            }

            nlohmann::json fieldResult =
                co_await (*resolver)(fieldArgs, nlohmann::json::object());

            // Apply field selection filtering if specified
            auto selectionIt = operation.fieldSelections.find(field);
            if (selectionIt != operation.fieldSelections.end() &&
                !selectionIt->second.empty())
            {
                fieldResult = filterFields(fieldResult, selectionIt->second);
            }

            result[field] = fieldResult;
        }
        else
        {
            throw std::runtime_error("Unknown query field: " + field);
        }
    }

    co_return result;
}

boost::asio::awaitable<nlohmann::json> LibGraphQLExecutor::executeMutation(
    const ParsedOperation& operation, const nlohmann::json& variables)
{
    nlohmann::json result;

    for (const auto& field : operation.selectionSet)
    {
        auto resolver = schema_->getMutation(field);
        if (resolver)
        {
            // Get field-specific arguments if available
            nlohmann::json fieldArgs = variables;
            auto argIt = operation.fieldArguments.find(field);
            if (argIt != operation.fieldArguments.end())
            {
                // Merge field arguments with variables, resolving variable
                // references
                for (auto it = argIt->second.begin(); it != argIt->second.end();
                     ++it)
                {
                    fieldArgs[it.key()] =
                        resolveVariables(it.value(), variables);
                }
            }

            nlohmann::json fieldResult =
                co_await (*resolver)(fieldArgs, nlohmann::json::object());

            // Apply field selection filtering if specified
            auto selectionIt = operation.fieldSelections.find(field);
            if (selectionIt != operation.fieldSelections.end() &&
                !selectionIt->second.empty())
            {
                fieldResult = filterFields(fieldResult, selectionIt->second);
            }

            result[field] = fieldResult;
        }
        else
        {
            throw std::runtime_error("Unknown mutation field: " + field);
        }
    }

    co_return result;
}

} // namespace NSNAME

// Made with Bob
