#pragma once

#include "name_space.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace NSNAME::graphql
{

struct Value
{
    enum class Kind
    {
        Int,
        Float,
        String,
        Boolean,
        Null,
        Enum,
        List,
        Object,
        Variable
    };

    Kind kind = Kind::Null;
    nlohmann::json value = nullptr;
};

struct Argument
{
    std::string name;
    Value value;
};

struct Directive
{
    std::string name;
    std::vector<Argument> arguments;
};

struct FieldSelection
{
    std::string name;
    std::string alias;
    std::vector<Argument> arguments;
    std::vector<Directive> directives;
    std::vector<FieldSelection> selections;
};

struct VariableDefinition
{
    std::string name;
    std::string typeName;
    bool nonNull = false;
    bool isList = false;
    std::optional<Value> defaultValue;
};

struct Operation
{
    enum class Type
    {
        Query,
        Mutation,
        Subscription
    };

    Type type = Type::Query;
    std::string name;
    std::vector<VariableDefinition> variableDefinitions;
    std::vector<Directive> directives;
    std::vector<FieldSelection> selections;
};

} // namespace NSNAME::graphql
