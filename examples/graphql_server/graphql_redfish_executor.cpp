#include "graphql_redfish_executor.hpp"

#include <stdexcept>

namespace NSNAME
{

namespace
{

nlohmann::json resolveVariableValue(const nlohmann::json& value,
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
            result[it.key()] = resolveVariableValue(it.value(), variables);
        }
        return result;
    }

    if (value.is_array())
    {
        nlohmann::json result = nlohmann::json::array();
        for (const auto& item : value)
        {
            result.push_back(resolveVariableValue(item, variables));
        }
        return result;
    }

    return value;
}

nlohmann::json initialVariables(const ParsedOperation& operation)
{
    nlohmann::json variables = nlohmann::json::object();
    for (const GraphQLVariableDefinition& definition : operation.variableDefinitions)
    {
        variables[definition.name] = definition.defaultValue
                                         ? definition.defaultValue->value
                                         : nlohmann::json(nullptr);
    }
    return variables;
}

} // namespace

RedfishGraphQLExecutor::RedfishGraphQLExecutor(
    GraphQLTypedSchema schema, std::shared_ptr<RedfishProvider> provider) :
    schema(std::move(schema)),
    provider(std::move(provider))
{}

boost::asio::awaitable<nlohmann::json> RedfishGraphQLExecutor::execute(
    const std::string& query, const nlohmann::json& variables)
{
    nlohmann::json response;

    try
    {
        std::string errorMsg;
        if (!LibGraphQLParser::validate(query, errorMsg))
        {
            response["errors"] =
                nlohmann::json::array({{{"message", errorMsg}}});
            co_return response;
        }

        ParsedOperation operation = LibGraphQLParser::parse(query);
        schema.validateOperation(operation);

        nlohmann::json mergedVariables = initialVariables(operation);
        for (auto it = variables.begin(); it != variables.end(); ++it)
        {
            mergedVariables[it.key()] = it.value();
        }

        response["data"] = co_await executeSelections(operation.selections,
                                                       mergedVariables);
    }
    catch (const std::exception& e)
    {
        response["errors"] = nlohmann::json::array({{{"message", e.what()}}});
    }

    co_return response;
}

boost::asio::awaitable<nlohmann::json> RedfishGraphQLExecutor::executeSelections(
    const std::vector<GraphQLFieldSelection>& selections,
    const nlohmann::json& variables)
{
    nlohmann::json result = nlohmann::json::object();
    for (const GraphQLFieldSelection& selection : selections)
    {
        std::string outputName =
            selection.alias.empty() ? selection.name : selection.alias;
        result[outputName] = co_await resolveRootField(selection, variables);
    }
    co_return result;
}

boost::asio::awaitable<nlohmann::json> RedfishGraphQLExecutor::resolveRootField(
    const GraphQLFieldSelection& selection, const nlohmann::json& variables)
{
    const GraphQLFieldSpec* fieldSpec = schema.getRootQueryField(selection.name);
    if (fieldSpec == nullptr)
    {
        throw std::runtime_error("Unknown query field: " + selection.name);
    }

    nlohmann::json args = resolveArguments(selection, variables);
    std::string target;
    if (selection.name == "serviceRoot")
    {
        target = "/redfish/v1";
    }
    else if (selection.name == "systems")
    {
        target = "/redfish/v1/Systems";
    }
    else if (selection.name == "system")
    {
        target = "/redfish/v1/Systems/" + args["id"].get<std::string>();
    }
    else if (selection.name == "chassis")
    {
        target = "/redfish/v1/Chassis";
    }
    else if (selection.name == "managers")
    {
        target = "/redfish/v1/Managers";
    }
    else if (selection.name == "ethernetInterfaces")
    {
        target = "/redfish/v1/Managers/bmc/EthernetInterfaces";
    }
    else if (selection.name == "managerNetworkProtocol")
    {
        target = "/redfish/v1/Managers/bmc/NetworkProtocol";
    }
    else
    {
        throw std::runtime_error("Unsupported Redfish root field: " +
                                 selection.name);
    }

    nlohmann::json payload = co_await provider->get(target);
    if (fieldSpec->isList)
    {
        if (!payload.contains("Members") || !payload["Members"].is_array())
        {
            throw std::runtime_error("Expected collection Members array for '" +
                                     selection.name + "'");
        }

        nlohmann::json result = nlohmann::json::array();
        for (const auto& member : payload["Members"])
        {
            if (!member.contains("@odata.id"))
            {
                continue;
            }
            nlohmann::json item =
                co_await provider->get(member["@odata.id"].get<std::string>());
            result.push_back(co_await projectObject(item, fieldSpec->returnType,
                                                    selection.selections));
        }
        co_return result;
    }

    if (fieldSpec->scalar)
    {
        co_return payload;
    }

    co_return co_await projectObject(payload, fieldSpec->returnType,
                                     selection.selections);
}

boost::asio::awaitable<nlohmann::json> RedfishGraphQLExecutor::projectField(
    const GraphQLFieldSelection& selection, const GraphQLFieldSpec& fieldSpec,
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
            throw std::runtime_error("Expected array for field '" + selection.name +
                                     "'");
        }

        nlohmann::json result = nlohmann::json::array();
        for (const auto& item : value)
        {
            result.push_back(
                co_await projectObject(item, fieldSpec.returnType, selection.selections));
        }
        co_return result;
    }

    co_return co_await projectObject(value, fieldSpec.returnType,
                                     selection.selections);
}

boost::asio::awaitable<nlohmann::json> RedfishGraphQLExecutor::projectObject(
    const nlohmann::json& source, const std::string& typeName,
    const std::vector<GraphQLFieldSelection>& selections)
{
    const GraphQLObjectSpec* objectSpec = schema.getObject(typeName);
    if (objectSpec == nullptr)
    {
        throw std::runtime_error("Unknown object type: " + typeName);
    }

    if (selections.empty())
    {
        co_return source;
    }

    nlohmann::json result = nlohmann::json::object();
    for (const GraphQLFieldSelection& selection : selections)
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

nlohmann::json RedfishGraphQLExecutor::resolveArguments(
    const GraphQLFieldSelection& selection, const nlohmann::json& variables) const
{
    nlohmann::json result = nlohmann::json::object();
    for (const GraphQLArgument& argument : selection.arguments)
    {
        result[argument.name] =
            resolveVariableValue(argument.value.value, variables);
    }
    return result;
}

} // namespace NSNAME
