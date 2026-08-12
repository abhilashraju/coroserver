#pragma once

#include "graphql/ast.hpp"
#include "name_space.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
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
    static TypedSchema fromFile(const std::string& path)
    {
        std::ifstream file(path);
        if (!file)
        {
            throw std::runtime_error("Cannot open schema file: " + path);
        }
        nlohmann::json doc = nlohmann::json::parse(file);
        return fromJson(doc);
    }

    // Load a TypedSchema from a pre-parsed nlohmann::json object.
    static TypedSchema fromJson(const nlohmann::json& doc)
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
                    fs.arguments.push_back({a.at("name").get<std::string>(),
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
                    fs.arguments.push_back({a.at("name").get<std::string>(),
                                            a.at("typeName").get<std::string>(),
                                            a.value("required", false)});
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

    void validateOperation(const Operation& operation) const
    {
        if (operation.type != Operation::Type::Query &&
            operation.type != Operation::Type::Subscription)
        {
            throw std::runtime_error(
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
                throw std::runtime_error("Unknown field: " + selection.name);
            }
            validateArguments(selection, *fieldSpec);
            if (!fieldSpec->scalar)
            {
                validateSelections(selection.selections, fieldSpec->returnType);
            }
            else if (!selection.selections.empty())
            {
                throw std::runtime_error(
                    "Scalar field cannot have sub-selections: " +
                    selection.name);
            }
        }
    }

  private:
    void validateSelections(const std::vector<FieldSelection>& selections,
                            const std::string& objectTypeName) const
    {
        const ObjectSpec* objectSpec = getObject(objectTypeName);
        if (objectSpec == nullptr)
        {
            throw std::runtime_error("Unknown object type: " + objectTypeName);
        }

        for (const FieldSelection& selection : selections)
        {
            auto fieldIt = objectSpec->fields.find(selection.name);
            if (fieldIt == objectSpec->fields.end())
            {
                throw std::runtime_error("Unknown field '" + selection.name +
                                         "' on type '" + objectTypeName + "'");
            }

            const FieldSpec& fieldSpec = fieldIt->second;
            validateArguments(selection, fieldSpec);
            if (fieldSpec.scalar)
            {
                if (!selection.selections.empty())
                {
                    throw std::runtime_error(
                        "Scalar field cannot have sub-selections: " +
                        selection.name);
                }
                continue;
            }

            validateSelections(selection.selections, fieldSpec.returnType);
        }
    }

    void validateArguments(const FieldSelection& field,
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
                throw std::runtime_error("Unknown argument '" + argument.name +
                                         "' on field '" + field.name + "'");
            }
        }

        for (const ArgumentSpec& spec : fieldSpec.arguments)
        {
            if (spec.required &&
                seenArguments.find(spec.name) == seenArguments.end())
            {
                throw std::runtime_error(
                    "Missing required argument '" + spec.name + "' on field '" +
                    field.name + "'");
            }
        }
    }

    std::unordered_map<std::string, FieldSpec> rootQueries;
    std::unordered_map<std::string, FieldSpec> rootSubscriptions;
    std::unordered_map<std::string, ObjectSpec> objects;
};

} // namespace NSNAME::graphql
