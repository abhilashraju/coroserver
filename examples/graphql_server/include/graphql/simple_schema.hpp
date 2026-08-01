#pragma once

#include "graphql/ast.hpp"
#include "name_space.hpp"

#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace NSNAME::graphql
{

using FieldResolver = std::function<boost::asio::awaitable<nlohmann::json>(
    const nlohmann::json& args, const nlohmann::json& context)>;

struct SimpleType
{
    std::string name;
    std::unordered_map<std::string, FieldResolver> fields;
};

class SimpleSchema
{
  public:
    void addType(const std::string& typeName, SimpleType type)
    {
        types[typeName] = std::move(type);
    }

    void addQuery(const std::string& queryName, FieldResolver resolver)
    {
        queries[queryName] = std::move(resolver);
    }

    void addMutation(const std::string& mutationName, FieldResolver resolver)
    {
        mutations[mutationName] = std::move(resolver);
    }

    std::optional<FieldResolver> getQuery(const std::string& name) const
    {
        auto it = queries.find(name);
        if (it == queries.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<FieldResolver> getMutation(const std::string& name) const
    {
        auto it = mutations.find(name);
        if (it == mutations.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    const SimpleType* getType(const std::string& name) const
    {
        auto it = types.find(name);
        if (it == types.end())
        {
            return nullptr;
        }
        return &it->second;
    }

  private:
    std::unordered_map<std::string, SimpleType> types;
    std::unordered_map<std::string, FieldResolver> queries;
    std::unordered_map<std::string, FieldResolver> mutations;
};

} // namespace NSNAME::graphql
