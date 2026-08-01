#include "graphql_redfish_schema.hpp"

namespace NSNAME
{

graphql::TypedSchema buildRedfishTypedSchema()
{
    graphql::TypedSchema schema;

    graphql::ObjectSpec serviceRoot{
        "ServiceRoot",
        {{"id", {"id", "Id", "String", false, true}},
         {"name", {"name", "Name", "String", false, true}},
         {"systems", {"systems", "Systems", "CollectionLink", false, false}},
         {"chassis", {"chassis", "Chassis", "CollectionLink", false, false}},
         {"managers", {"managers", "Managers", "CollectionLink", false, false}}}};

    graphql::ObjectSpec collectionLink{
        "CollectionLink",
        {{"odataId", {"odataId", "@odata.id", "String", false, true}}}};

    graphql::ObjectSpec status{
        "Status",
        {{"health", {"health", "Health", "String", false, true}},
         {"state", {"state", "State", "String", false, true}}}};

    graphql::ObjectSpec processorSummary{
        "ProcessorSummary",
        {{"count", {"count", "Count", "Int", false, true}},
         {"model", {"model", "Model", "String", false, true}}}};

    graphql::ObjectSpec ipv4Address{
        "IPv4Address",
        {{"address", {"address", "Address", "String", false, true}},
         {"subnetMask", {"subnetMask", "SubnetMask", "String", false, true}},
         {"gateway", {"gateway", "Gateway", "String", false, true}}}};

    graphql::ObjectSpec ipv6Address{
        "IPv6Address",
        {{"address", {"address", "Address", "String", false, true}},
         {"prefixLength", {"prefixLength", "PrefixLength", "Int", false, true}},
         {"addressState", {"addressState", "AddressState", "String", false, true}}}};

    graphql::ObjectSpec ntpInfo{
        "NtpInfo",
        {{"protocolEnabled",
          {"protocolEnabled", "ProtocolEnabled", "Boolean", false, true}},
         {"ntpServers", {"ntpServers", "NTPServers", "String", true, true}}}};

    graphql::ObjectSpec managerNetworkProtocol{
        "ManagerNetworkProtocol",
        {{"ntp", {"ntp", "NTP", "NtpInfo", false, false}}}};

    graphql::ObjectSpec computerSystem{
        "ComputerSystem",
        {{"id", {"id", "Id", "String", false, true}},
         {"name", {"name", "Name", "String", false, true}},
         {"powerState", {"powerState", "PowerState", "String", false, true}},
         {"status", {"status", "Status", "Status", false, false}},
         {"processorSummary",
          {"processorSummary", "ProcessorSummary", "ProcessorSummary", false,
           false}}}};

    graphql::ObjectSpec chassis{
        "Chassis",
        {{"id", {"id", "Id", "String", false, true}},
         {"name", {"name", "Name", "String", false, true}},
         {"status", {"status", "Status", "Status", false, false}}}};

    graphql::ObjectSpec ethernetInterface{
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

    graphql::ObjectSpec manager{
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
    schema.addRootQuery(
        {"system", "", "ComputerSystem", false, false, {{"id", "ID", true}}});
    schema.addRootQuery({"chassis", "", "Chassis", true, false});
    schema.addRootQuery({"managers", "", "Manager", true, false});
    schema.addRootQuery({"ethernetInterfaces", "", "EthernetInterface", true,
                         false});
    schema.addRootQuery(
        {"managerNetworkProtocol", "", "ManagerNetworkProtocol", false, false});

    // Subscriptions — clients can listen for periodic updates on these fields
    schema.addRootSubscription(
        {"systemStatus", "", "ComputerSystem", false, false, {{"id", "ID", true}}});
    schema.addRootSubscription(
        {"chassisStatus", "", "Chassis", false, false, {{"id", "ID", true}}});
    schema.addRootSubscription(
        {"ethernetInterfaceUpdates", "", "EthernetInterface", true, false});

    return schema;
}

} // namespace NSNAME
