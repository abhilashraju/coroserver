#pragma once

#include "graphql/ast.hpp"

#include <nlohmann/json.hpp>

#include <vector>

namespace NSNAME::graphql
{

inline nlohmann::json resolveVariables(const nlohmann::json& value,
                                      const nlohmann::json& variables)
{
    if (value.is_object())
    {
        if (value.contains("$variable") && value.size() == 1)
        {
            const std::string& variableName =
                value["$variable"].get<std::string>();
            if (variables.contains(variableName))
            {
                return variables[variableName];
            }
            return nullptr;
        }

        nlohmann::json result = nlohmann::json::object();
        for (auto it = value.begin(); it != value.end(); ++it)
        {
            result[it.key()] = resolveVariables(it.value(), variables);
        }
        return result;
    }

    if (value.is_array())
    {
        nlohmann::json result = nlohmann::json::array();
        for (const auto& item : value)
        {
            result.push_back(resolveVariables(item, variables));
        }
        return result;
    }

    return value;
}

inline nlohmann::json argumentsToJson(const std::vector<Argument>& arguments,
                                      const nlohmann::json& variables)
{
    nlohmann::json result = nlohmann::json::object();
    for (const Argument& argument : arguments)
    {
        result[argument.name] = resolveVariables(argument.value.value, variables);
    }
    return result;
}

inline nlohmann::json initialVariables(const Operation& operation)
{
    nlohmann::json result = nlohmann::json::object();
    for (const VariableDefinition& definition : operation.variableDefinitions)
    {
        result[definition.name] = definition.defaultValue
                                      ? definition.defaultValue->value
                                      : nlohmann::json(nullptr);
    }
    return result;
}

inline nlohmann::json filterFields(
    const nlohmann::json& data, const std::vector<FieldSelection>& selections)
{
    if (selections.empty())
    {
        return data;
    }
    if (data.is_array())
    {
        nlohmann::json result = nlohmann::json::array();
        for (const auto& item : data)
        {
            result.push_back(filterFields(item, selections));
        }
        return result;
    }
    if (data.is_object())
    {
        nlohmann::json result = nlohmann::json::object();
        for (const FieldSelection& selection : selections)
        {
            const std::string& sourceName = selection.name;
            std::string outputName =
                selection.alias.empty() ? selection.name : selection.alias;
            if (!data.contains(sourceName))
            {
                continue;
            }
            if (selection.selections.empty())
            {
                result[outputName] = data[sourceName];
            }
            else
            {
                result[outputName] =
                    filterFields(data[sourceName], selection.selections);
            }
        }
        return result;
    }
    return data;
}

} // namespace NSNAME::graphql
