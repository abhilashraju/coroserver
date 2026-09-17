#pragma once

#include "graphql/ast.hpp"
#include "graphql/error.hpp"
#include "name_space.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace NSNAME::graphql
{

struct ArgumentSpec
{
    std::string name;
    std::string typeName;
    bool required = false;
    // Default value applied when the caller omits this argument.
    // Stored as a string because all Redfish path placeholders are strings.
    std::string defaultValue;
};

struct FieldSpec
{
    std::string name;
    std::string responseKey;
    std::string returnType;
    bool isList = false;
    bool scalar = false;
    std::vector<ArgumentSpec> arguments;
    // Redfish URL template for root queries/subscriptions.
    // Use {argName} placeholders that are substituted from query arguments.
    // Example: "/redfish/v1/Systems/{id}"
    // Leave empty for object fields (non-root).
    std::string redfishPath;
    // When true the collection URL is fetched with ?$expand=*($levels=1) so
    // all member data is inlined in a single response, avoiding one HTTP
    // request per member.  Only set this for Redfish resources that support
    // the $expand query parameter (e.g. Sensors on OpenBMC/bmcweb).
    bool expandMembers = false;
    // When true this isList field holds a Redfish collection link at
    // projection time — i.e. the JSON value is {"@odata.id": "<url>"}
    // rather than an inline array.  The executor will fetch that URL with
    // ?$expand=*($levels=1) and project the resulting Members array in one
    // request.  Use for sub-collections embedded in a parent object
    // (e.g. PCIeFunctions inside a PCIeDevice body).
    bool collectionLink = false;
};

struct ObjectSpec
{
    std::string name;
    std::unordered_map<std::string, FieldSpec> fields;
};

// Maps a DBus object/interface/property tuple to one or more GraphQL
// subscription field names that should be woken when the signal fires.
// dbusObject and dbusProperty may be empty to act as wildcards (match any).
// dbusInterface is required.
struct DbusWatcher
{
    std::string dbusObject;    // e.g. "/xyz/openbmc_project/state/host0"  (empty = any object)
    std::string dbusInterface; // e.g. "xyz.openbmc_project.State.Host"
    std::string dbusProperty;  // e.g. "CurrentHostState"  (empty = any property on interface)
    std::vector<std::string> graphqlFields; // subscription field names to wake
};

class TypedSchema
{
  public:
    // Load a TypedSchema from a JSON file.
    // The file must follow the schema described in redfish_schema.json.
    static Result<TypedSchema> fromFile(const std::string& path)
    {
        std::ifstream file(path);
        if (!file)
        {
            return std::unexpected("Cannot open schema file: " + path);
        }
        nlohmann::json doc = nlohmann::json::parse(file, nullptr, false);
        if (doc.is_discarded())
        {
            return std::unexpected("Invalid JSON in schema file: " + path);
        }
        return fromJson(doc);
    }

    // Load a TypedSchema from a pre-parsed nlohmann::json object.
    // Returns std::unexpected if the document is structurally invalid.
    static Result<TypedSchema> fromJson(const nlohmann::json& doc)
    {
        // nlohmann::json::at() throws on missing keys; contain it here so the
        // rest of the stack never sees exceptions from schema loading.
        try
        {
            TypedSchema schema;

            for (const auto& obj : doc.at("objects"))
            {
                ObjectSpec objectSpec;
                objectSpec.name = obj.at("name").get<std::string>();
                for (const auto& f : obj.at("fields"))
                {
                    FieldSpec fs;
                    fs.name = f.at("name").get<std::string>();
                    fs.responseKey = f.at("responseKey").get<std::string>();
                    fs.returnType = f.at("returnType").get<std::string>();
                    fs.isList = f.value("isList", false);
                    fs.scalar = f.value("scalar", false);
                    fs.collectionLink = f.value("collectionLink", false);
                    for (const auto& a :
                         f.value("arguments", nlohmann::json::array()))
                    {
                        fs.arguments.push_back(
                            {a.at("name").get<std::string>(),
                             a.at("typeName").get<std::string>(),
                             a.value("required", false)});
                    }
                    objectSpec.fields[fs.name] = std::move(fs);
                }
                schema.addObject(std::move(objectSpec));
            }

            auto loadFields =
                [](const nlohmann::json& arr) -> std::vector<FieldSpec> {
                std::vector<FieldSpec> out;
                for (const auto& f : arr)
                {
                    FieldSpec fs;
                    fs.name = f.at("name").get<std::string>();
                    fs.responseKey = f.value("responseKey", std::string{});
                    fs.returnType = f.at("returnType").get<std::string>();
                    fs.isList = f.value("isList", false);
                    fs.scalar = f.value("scalar", false);
                    fs.redfishPath = f.value("redfishPath", std::string{});
                    fs.expandMembers = f.value("expandMembers", false);
                    for (const auto& a :
                         f.value("arguments", nlohmann::json::array()))
                    {
                        fs.arguments.push_back(
                            {a.at("name").get<std::string>(),
                             a.at("typeName").get<std::string>(),
                             a.value("required", false),
                             a.value("default", std::string{})});
                    }
                    out.push_back(std::move(fs));
                }
                return out;
            };

            for (auto& fs :
                 loadFields(doc.value("queries", nlohmann::json::array())))
            {
                schema.addRootQuery(std::move(fs));
            }
            for (auto& fs :
                 loadFields(doc.value("subscriptions", nlohmann::json::array())))
            {
                schema.addRootSubscription(std::move(fs));
            }

            for (const auto& entry :
                 doc.value("dbusWatchers", nlohmann::json::array()))
            {
                DbusWatcher w;
                w.dbusObject = entry.value("dbusObject", std::string{});
                w.dbusInterface = entry.at("dbusInterface").get<std::string>();
                w.dbusProperty = entry.value("dbusProperty", std::string{});
                w.graphqlFields =
                    entry.at("graphqlFields").get<std::vector<std::string>>();
                schema.addDbusWatcher(std::move(w));
            }

            return schema;
        }
        catch (const std::exception& e)
        {
            return std::unexpected(
                std::string("Schema JSON parse error: ") + e.what());
        }
    }

    void addObject(ObjectSpec objectSpec)
    {
        objects[objectSpec.name] = std::move(objectSpec);
    }

    void addRootQuery(FieldSpec fieldSpec)
    {
        rootQueries[fieldSpec.name] = std::move(fieldSpec);
    }

    void addRootSubscription(FieldSpec fieldSpec)
    {
        rootSubscriptions[fieldSpec.name] = std::move(fieldSpec);
    }

    void addDbusWatcher(DbusWatcher w)
    {
        dbusWatchers.push_back(std::move(w));
    }

    const std::vector<DbusWatcher>& getDbusWatchers() const
    {
        return dbusWatchers;
    }

    const FieldSpec* getRootQueryField(const std::string& name) const
    {
        auto it = rootQueries.find(name);
        if (it == rootQueries.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    const FieldSpec* getRootSubscriptionField(const std::string& name) const
    {
        auto it = rootSubscriptions.find(name);
        if (it == rootSubscriptions.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    const ObjectSpec* getObject(const std::string& name) const
    {
        auto it = objects.find(name);
        if (it == objects.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    Result<void> validateOperation(const Operation& operation) const
    {
        if (operation.type != Operation::Type::Query &&
            operation.type != Operation::Type::Subscription)
        {
            return std::unexpected(
                "Only query and subscription operations are supported");
        }

        for (const FieldSelection& selection : operation.selections)
        {
            const FieldSpec* fieldSpec =
                (operation.type == Operation::Type::Subscription)
                    ? getRootSubscriptionField(selection.name)
                    : getRootQueryField(selection.name);
            if (fieldSpec == nullptr)
            {
                return std::unexpected("Unknown field: " + selection.name);
            }
            if (auto r = validateArguments(selection, *fieldSpec); !r)
            {
                return r;
            }
            if (!fieldSpec->scalar)
            {
                if (auto r = validateSelections(selection.selections,
                                                fieldSpec->returnType);
                    !r)
                {
                    return r;
                }
            }
            else if (!selection.selections.empty())
            {
                return std::unexpected(
                    "Scalar field cannot have sub-selections: " +
                    selection.name);
            }
        }
        return {};
    }

  private:
    Result<void> validateSelections(
        const std::vector<FieldSelection>& selections,
        const std::string& objectTypeName) const
    {
        const ObjectSpec* objectSpec = getObject(objectTypeName);
        if (objectSpec == nullptr)
        {
            return std::unexpected("Unknown object type: " + objectTypeName);
        }

        for (const FieldSelection& selection : selections)
        {
            auto fieldIt = objectSpec->fields.find(selection.name);
            if (fieldIt == objectSpec->fields.end())
            {
                return std::unexpected("Unknown field '" + selection.name +
                                       "' on type '" + objectTypeName + "'");
            }

            const FieldSpec& fieldSpec = fieldIt->second;
            if (auto r = validateArguments(selection, fieldSpec); !r)
            {
                return r;
            }
            if (fieldSpec.scalar)
            {
                if (!selection.selections.empty())
                {
                    return std::unexpected(
                        "Scalar field cannot have sub-selections: " +
                        selection.name);
                }
                continue;
            }

            if (auto r =
                    validateSelections(selection.selections, fieldSpec.returnType);
                !r)
            {
                return r;
            }
        }
        return {};
    }

    Result<void> validateArguments(const FieldSelection& field,
                                   const FieldSpec& fieldSpec) const
    {
        std::unordered_map<std::string, bool> seenArguments;
        for (const Argument& argument : field.arguments)
        {
            bool found = false;
            for (const ArgumentSpec& spec : fieldSpec.arguments)
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
                return std::unexpected("Unknown argument '" + argument.name +
                                       "' on field '" + field.name + "'");
            }
        }

        for (const ArgumentSpec& spec : fieldSpec.arguments)
        {
            if (spec.required &&
                seenArguments.find(spec.name) == seenArguments.end())
            {
                return std::unexpected(
                    "Missing required argument '" + spec.name +
                    "' on field '" + field.name + "'");
            }
        }
        return {};
    }

    std::unordered_map<std::string, FieldSpec> rootQueries;
    std::unordered_map<std::string, FieldSpec> rootSubscriptions;
    std::unordered_map<std::string, ObjectSpec> objects;
    std::vector<DbusWatcher> dbusWatchers;
};

} // namespace NSNAME::graphql
