#include "graphql_redfish_schema.hpp"

#include <stdexcept>

namespace NSNAME
{

void GraphQLTypedSchema::addObject(GraphQLObjectSpec objectSpec)
{
    objects[objectSpec.name] = std::move(objectSpec);
}

void GraphQLTypedSchema::addRootQuery(GraphQLFieldSpec fieldSpec)
{
    rootQueries[fieldSpec.name] = std::move(fieldSpec);
}

const GraphQLFieldSpec* GraphQLTypedSchema::getRootQueryField(
    const std::string& name) const
{
    auto it = rootQueries.find(name);
    if (it == rootQueries.end())
    {
        return nullptr;
    }
    return &it->second;
}

const GraphQLObjectSpec* GraphQLTypedSchema::getObject(
    const std::string& name) const
{
    auto it = objects.find(name);
    if (it == objects.end())
    {
        return nullptr;
    }
    return &it->second;
}

void GraphQLTypedSchema::validateOperation(const ParsedOperation& operation) const
{
    if (operation.type != ParsedOperation::OperationType::Query)
    {
        throw std::runtime_error("Only query operations are supported");
    }

    for (const GraphQLFieldSelection& selection : operation.selections)
    {
        const GraphQLFieldSpec* fieldSpec = getRootQueryField(selection.name);
        if (fieldSpec == nullptr)
        {
            throw std::runtime_error("Unknown query field: " + selection.name);
        }
        validateArguments(selection, *fieldSpec);
        if (!fieldSpec->scalar)
        {
            validateSelections(selection.selections, fieldSpec->returnType);
        }
        else if (!selection.selections.empty())
        {
            throw std::runtime_error("Scalar field cannot have sub-selections: " +
                                     selection.name);
        }
    }
}

void GraphQLTypedSchema::validateSelections(
    const std::vector<GraphQLFieldSelection>& selections,
    const std::string& objectTypeName) const
{
    const GraphQLObjectSpec* objectSpec = getObject(objectTypeName);
    if (objectSpec == nullptr)
    {
        throw std::runtime_error("Unknown object type: " + objectTypeName);
    }

    for (const GraphQLFieldSelection& selection : selections)
    {
        auto fieldIt = objectSpec->fields.find(selection.name);
        if (fieldIt == objectSpec->fields.end())
        {
            throw std::runtime_error("Unknown field '" + selection.name +
                                     "' on type '" + objectTypeName + "'");
        }

        const GraphQLFieldSpec& fieldSpec = fieldIt->second;
        validateArguments(selection, fieldSpec);
        if (fieldSpec.scalar)
        {
            if (!selection.selections.empty())
            {
                throw std::runtime_error(
                    "Scalar field cannot have sub-selections: " + selection.name);
            }
            continue;
        }

        validateSelections(selection.selections, fieldSpec.returnType);
    }
}

void GraphQLTypedSchema::validateArguments(const GraphQLFieldSelection& field,
                                           const GraphQLFieldSpec& fieldSpec) const
{
    std::unordered_map<std::string, bool> seenArguments;
    for (const GraphQLArgument& argument : field.arguments)
    {
        bool found = false;
        for (const GraphQLArgumentSpec& spec : fieldSpec.arguments)
        {
            if (spec.name == argument.name)
            {
                found = true;
                seenArguments[spec.name] = true;
                break;
            }
        }
        if (!found)
        {
            throw std::runtime_error("Unknown argument '" + argument.name +
                                     "' on field '" + field.name + "'");
        }
    }

    for (const GraphQLArgumentSpec& spec : fieldSpec.arguments)
    {
        if (spec.required && seenArguments.find(spec.name) == seenArguments.end())
        {
            throw std::runtime_error("Missing required argument '" + spec.name +
                                     "' on field '" + field.name + "'");
        }
    }
}

GraphQLTypedSchema buildRedfishTypedSchema()
{
    GraphQLTypedSchema schema;

    GraphQLObjectSpec serviceRoot{"ServiceRoot",
                                  {{"id", {"id", "Id", "String", false, true}},
                                   {"name",
                                    {"name", "Name", "String", false, true}},
                                   {"systems",
                                    {"systems", "Systems", "CollectionLink", false,
                                     false}},
                                   {"chassis",
                                    {"chassis", "Chassis", "CollectionLink", false,
                                     false}},
                                   {"managers",
                                    {"managers", "Managers", "CollectionLink", false,
                                     false}}}};

    GraphQLObjectSpec collectionLink{
        "CollectionLink",
        {{"odataId", {"odataId", "@odata.id", "String", false, true}}}};

    GraphQLObjectSpec status{"Status",
                             {{"health",
                               {"health", "Health", "String", false, true}},
                              {"state",
                               {"state", "State", "String", false, true}}}};

    GraphQLObjectSpec processorSummary{
        "ProcessorSummary",
        {{"count", {"count", "Count", "Int", false, true}},
         {"model", {"model", "Model", "String", false, true}}}};

    GraphQLObjectSpec ipv4Address{
        "IPv4Address",
        {{"address", {"address", "Address", "String", false, true}},
         {"subnetMask", {"subnetMask", "SubnetMask", "String", false, true}},
         {"gateway", {"gateway", "Gateway", "String", false, true}}}};

    GraphQLObjectSpec ipv6Address{
        "IPv6Address",
        {{"address", {"address", "Address", "String", false, true}},
         {"prefixLength", {"prefixLength", "PrefixLength", "Int", false, true}},
         {"addressState", {"addressState", "AddressState", "String", false, true}}}};

    GraphQLObjectSpec ntpInfo{
        "NtpInfo",
        {{"protocolEnabled",
          {"protocolEnabled", "ProtocolEnabled", "Boolean", false, true}},
         {"ntpServers", {"ntpServers", "NTPServers", "String", true, true}}}};

    GraphQLObjectSpec managerNetworkProtocol{
        "ManagerNetworkProtocol",
        {{"ntp", {"ntp", "NTP", "NtpInfo", false, false}}}};

    GraphQLObjectSpec computerSystem{
        "ComputerSystem",
        {{"id", {"id", "Id", "String", false, true}},
         {"name", {"name", "Name", "String", false, true}},
         {"powerState", {"powerState", "PowerState", "String", false, true}},
         {"status", {"status", "Status", "Status", false, false}},
         {"processorSummary",
          {"processorSummary", "ProcessorSummary", "ProcessorSummary", false,
           false}}}};

    GraphQLObjectSpec chassis{
        "Chassis",
        {{"id", {"id", "Id", "String", false, true}},
         {"name", {"name", "Name", "String", false, true}},
         {"status", {"status", "Status", "Status", false, false}}}};

    GraphQLObjectSpec ethernetInterface{
        "EthernetInterface",
        {{"id", {"id", "Id", "String", false, true}},
         {"name", {"name", "Name", "String", false, true}},
         {"macAddress", {"macAddress", "MACAddress", "String", false, true}},
         {"speedMbps", {"speedMbps", "SpeedMbps", "Int", false, true}},
         {"linkStatus", {"linkStatus", "LinkStatus", "String", false, true}},
         {"ipv4Addresses",
          {"ipv4Addresses", "IPv4Addresses", "IPv4Address", true, false}},
         {"ipv6Addresses",
          {"ipv6Addresses", "IPv6Addresses", "IPv6Address", true, false}},
         {"status", {"status", "Status", "Status", false, false}}}};

    GraphQLObjectSpec manager{
        "Manager",
        {{"id", {"id", "Id", "String", false, true}},
         {"name", {"name", "Name", "String", false, true}},
         {"status", {"status", "Status", "Status", false, false}}}};

    schema.addObject(std::move(serviceRoot));
    schema.addObject(std::move(collectionLink));
    schema.addObject(std::move(status));
    schema.addObject(std::move(processorSummary));
    schema.addObject(std::move(ipv4Address));
    schema.addObject(std::move(ipv6Address));
    schema.addObject(std::move(ntpInfo));
    schema.addObject(std::move(managerNetworkProtocol));
    schema.addObject(std::move(computerSystem));
    schema.addObject(std::move(chassis));
    schema.addObject(std::move(ethernetInterface));
    schema.addObject(std::move(manager));

    schema.addRootQuery({"serviceRoot", "", "ServiceRoot", false, false});
    schema.addRootQuery({"systems", "", "ComputerSystem", true, false});
    schema.addRootQuery({"system",
                         "",
                         "ComputerSystem",
                         false,
                         false,
                         {{"id", "ID", true}}});
    schema.addRootQuery({"chassis", "", "Chassis", true, false});
    schema.addRootQuery({"managers", "", "Manager", true, false});
    schema.addRootQuery({"ethernetInterfaces", "", "EthernetInterface", true,
                         false});
    schema.addRootQuery(
        {"managerNetworkProtocol", "", "ManagerNetworkProtocol", false, false});

    return schema;
}

} // namespace NSNAME
