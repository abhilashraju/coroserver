#include "graphql_redfish_executor.hpp"

#include <stdexcept>

namespace NSNAME
{

boost::asio::awaitable<nlohmann::json> RedfishGraphQLExecutor::resolveRootField(
    const graphql::FieldSelection& selection, const graphql::FieldSpec& fieldSpec,
    const nlohmann::json& variables)
{
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
    if (fieldSpec.isList)
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
            result.push_back(
                co_await projectObject(item, fieldSpec.returnType, selection.selections));
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

} // namespace NSNAME
