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
};

struct ObjectSpec
{
    std::string name;
    std::unordered_map<std::string, FieldSpec> fields;
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
};

} // namespace NSNAME::graphql
