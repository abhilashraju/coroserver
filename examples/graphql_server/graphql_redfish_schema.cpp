#include "graphql_redfish_schema.hpp"

#include <filesystem>
#include <string>

namespace NSNAME
{

namespace
{
// Default path for the JSON schema file. Override at build time by defining
// REDFISH_SCHEMA_JSON_PATH.
#ifndef REDFISH_SCHEMA_JSON_PATH
constexpr const char* defaultSchemaPath =
    "/usr/share/graphql_redfish_server/redfish_schema.json";
#else
constexpr const char* defaultSchemaPath = REDFISH_SCHEMA_JSON_PATH;
#endif
} // namespace

NSNAME::graphql::Result<NSNAME::graphql::TypedSchema> buildRedfishTypedSchema()
{
    // Try to load the schema from the JSON file at runtime so that it can be
    // changed without recompilation.  Fall back to the hardcoded definition if
    // the file is not present.
    if (std::filesystem::exists(defaultSchemaPath))
    {
        return graphql::TypedSchema::fromFile(defaultSchemaPath);
    }

    // --- Hardcoded fallback (mirrors redfish_schema.json) ---
    graphql::TypedSchema schema;

#if 0
    schema.addObject(
        {"ServiceRoot",
         {{"id", {"id", "Id", "String", false, true}},
          {"name", {"name", "Name", "String", false, true}},
          {"systems", {"systems", "Systems", "CollectionLink", false, false}},
          {"chassis", {"chassis", "Chassis", "CollectionLink", false, false}},
          {"managers",
           {"managers", "Managers", "CollectionLink", false, false}}}});

    schema.addObject(
        {"CollectionLink",
         {{"odataId", {"odataId", "@odata.id", "String", false, true}}}});

    schema.addObject({"Status",
                      {{"health", {"health", "Health", "String", false, true}},
                       {"state", {"state", "State", "String", false, true}}}});

    schema.addObject({"ProcessorSummary",
                      {{"count", {"count", "Count", "Int", false, true}},
                       {"model", {"model", "Model", "String", false, true}}}});

    schema.addObject(
        {"IPv4Address",
         {{"address", {"address", "Address", "String", false, true}},
          {"subnetMask", {"subnetMask", "SubnetMask", "String", false, true}},
          {"gateway", {"gateway", "Gateway", "String", false, true}}}});

    schema.addObject(
        {"IPv6Address",
         {{"address", {"address", "Address", "String", false, true}},
          {"prefixLength",
           {"prefixLength", "PrefixLength", "Int", false, true}},
          {"addressState",
           {"addressState", "AddressState", "String", false, true}}}});

    schema.addObject(
        {"NtpInfo",
         {{"protocolEnabled",
           {"protocolEnabled", "ProtocolEnabled", "Boolean", false, true}},
          {"ntpServers", {"ntpServers", "NTPServers", "String", true, true}}}});

    schema.addObject({"ManagerNetworkProtocol",
                      {{"ntp", {"ntp", "NTP", "NtpInfo", false, false}}}});

    schema.addObject(
        {"ComputerSystem",
         {{"id", {"id", "Id", "String", false, true}},
          {"name", {"name", "Name", "String", false, true}},
          {"powerState", {"powerState", "PowerState", "String", false, true}},
          {"status", {"status", "Status", "Status", false, false}},
          {"processorSummary",
           {"processorSummary", "ProcessorSummary", "ProcessorSummary", false,
            false}}}});

    schema.addObject(
        {"Chassis",
         {{"id",           {"id",           "Id",           "String", false, true}},
          {"name",         {"name",         "Name",         "String", false, true}},
          {"chassisType",  {"chassisType",  "ChassisType",  "String", false, true}},
          {"manufacturer", {"manufacturer", "Manufacturer", "String", false, true}},
          {"model",        {"model",        "Model",        "String", false, true}},
          {"serialNumber", {"serialNumber", "SerialNumber", "String", false, true}},
          {"partNumber",   {"partNumber",   "PartNumber",   "String", false, true}},
          {"assetTag",     {"assetTag",     "AssetTag",     "String", false, true}},
          {"sku",          {"sku",          "SKU",          "String", false, true}},
          {"powerState",   {"powerState",   "PowerState",   "String", false, true}},
          {"indicatorLed", {"indicatorLed", "IndicatorLED", "String", false, true}},
          {"status",       {"status",       "Status",       "Status", false, false}}}});

    schema.addObject(
        {"EthernetInterface",
         {{"id", {"id", "Id", "String", false, true}},
          {"name", {"name", "Name", "String", false, true}},
          {"macAddress", {"macAddress", "MACAddress", "String", false, true}},
          {"speedMbps", {"speedMbps", "SpeedMbps", "Int", false, true}},
          {"linkStatus", {"linkStatus", "LinkStatus", "String", false, true}},
          {"ipv4Addresses",
           {"ipv4Addresses", "IPv4Addresses", "IPv4Address", true, false}},
          {"ipv6Addresses",
           {"ipv6Addresses", "IPv6Addresses", "IPv6Address", true, false}},
          {"status", {"status", "Status", "Status", false, false}}}});

    schema.addObject(
        {"Manager",
         {{"id", {"id", "Id", "String", false, true}},
          {"name", {"name", "Name", "String", false, true}},
          {"status", {"status", "Status", "Status", false, false}}}});

    schema.addRootQuery(
        {"serviceRoot", "", "ServiceRoot", false, false, {}, "/redfish/v1"});
    schema.addRootQuery(
        {"systems",
         "",
         "ComputerSystem",
         true,
         false,
         {},
         "/redfish/v1/Systems"});
    schema.addRootQuery(
        {"system",
         "",
         "ComputerSystem",
         false,
         false,
         {{"id", "ID", true}},
         "/redfish/v1/Systems/{id}"});
    schema.addRootQuery(
        {"chassis", "", "Chassis", true, false, {}, "/redfish/v1/Chassis"});
    schema.addRootQuery(
        {"managers", "", "Manager", true, false, {}, "/redfish/v1/Managers"});
    schema.addRootQuery(
        {"ethernetInterfaces",
         "",
         "EthernetInterface",
         true,
         false,
         {},
         "/redfish/v1/Managers/bmc/EthernetInterfaces"});
    schema.addRootQuery(
        {"managerNetworkProtocol",
         "",
         "ManagerNetworkProtocol",
         false,
         false,
         {},
         "/redfish/v1/Managers/bmc/NetworkProtocol"});

    schema.addRootSubscription(
        {"systemStatus",
         "",
         "ComputerSystem",
         false,
         false,
         {{"id", "ID", true}},
         "/redfish/v1/Systems/{id}"});
    schema.addRootSubscription(
        {"chassisStatus",
         "",
         "Chassis",
         false,
         false,
         {{"id", "ID", true}},
         "/redfish/v1/Chassis/{id}"});
    schema.addRootSubscription(
        {"chassisUpdates",
         "",
         "Chassis",
         true,
         false,
         {},
         "/redfish/v1/Chassis"});
    schema.addRootSubscription(
        {"ethernetInterfaceUpdates",
         "",
         "EthernetInterface",
         true,
         false,
         {},
         "/redfish/v1/Managers/bmc/EthernetInterfaces"});
#endif
    return NSNAME::graphql::Result<NSNAME::graphql::TypedSchema>(
        std::move(schema));
}

} // namespace NSNAME
