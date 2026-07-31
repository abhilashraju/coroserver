#pragma once

#include "graphql_parser_libgraphql.hpp"
#include "graphql_redfish_provider.hpp"
#include "graphql_redfish_schema.hpp"
#include "name_space.hpp"

#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>

namespace NSNAME
{

class RedfishGraphQLExecutor
{
  public:
    RedfishGraphQLExecutor(GraphQLTypedSchema schema,
                           std::shared_ptr<RedfishProvider> provider);

    boost::asio::awaitable<nlohmann::json> execute(
        const std::string& query,
        const nlohmann::json& variables = nlohmann::json::object());

  private:
    boost::asio::awaitable<nlohmann::json> executeSelections(
        const std::vector<GraphQLFieldSelection>& selections,
        const nlohmann::json& variables);
    boost::asio::awaitable<nlohmann::json> resolveRootField(
        const GraphQLFieldSelection& selection, const nlohmann::json& variables);
    boost::asio::awaitable<nlohmann::json> projectField(
        const GraphQLFieldSelection& selection, const GraphQLFieldSpec& fieldSpec,
        const nlohmann::json& source);
    boost::asio::awaitable<nlohmann::json> projectObject(
        const nlohmann::json& source, const std::string& typeName,
        const std::vector<GraphQLFieldSelection>& selections);
    nlohmann::json resolveArguments(const GraphQLFieldSelection& selection,
                                    const nlohmann::json& variables) const;

    GraphQLTypedSchema schema;
    std::shared_ptr<RedfishProvider> provider;
};

} // namespace NSNAME
