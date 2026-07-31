#pragma once

#include "graphql_parser_libgraphql.hpp"
#include "name_space.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace NSNAME
{

struct GraphQLArgumentSpec
{
    std::string name;
    std::string typeName;
    bool required = false;
};

struct GraphQLFieldSpec
{
    std::string name;
    std::string responseKey;
    std::string returnType;
    bool isList = false;
    bool scalar = false;
    std::vector<GraphQLArgumentSpec> arguments;
};

struct GraphQLObjectSpec
{
    std::string name;
    std::unordered_map<std::string, GraphQLFieldSpec> fields;
};

class GraphQLTypedSchema
{
  public:
    void addObject(GraphQLObjectSpec objectSpec);
    void addRootQuery(GraphQLFieldSpec fieldSpec);

    const GraphQLFieldSpec* getRootQueryField(const std::string& name) const;
    const GraphQLObjectSpec* getObject(const std::string& name) const;

    void validateOperation(const ParsedOperation& operation) const;

  private:
    void validateSelections(const std::vector<GraphQLFieldSelection>& selections,
                            const std::string& objectTypeName) const;
    void validateArguments(const GraphQLFieldSelection& field,
                           const GraphQLFieldSpec& fieldSpec) const;

    std::unordered_map<std::string, GraphQLFieldSpec> rootQueries;
    std::unordered_map<std::string, GraphQLObjectSpec> objects;
};

GraphQLTypedSchema buildRedfishTypedSchema();

} // namespace NSNAME
