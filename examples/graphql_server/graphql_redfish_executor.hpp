#pragma once

#include "graphql/typed_executor.hpp"
#include "graphql_redfish_provider.hpp"
#include "graphql_redfish_schema.hpp"

#include <memory>
#include <string>

namespace NSNAME
{

class RedfishGraphQLExecutor : public graphql::TypedExecutor<RedfishProvider>
{
  public:
    RedfishGraphQLExecutor(graphql::TypedSchema schema,
                           std::shared_ptr<RedfishProvider> provider) :
        graphql::TypedExecutor<RedfishProvider>(std::move(schema),
                                                std::move(provider))
    {}

  protected:
    boost::asio::awaitable<nlohmann::json> resolveRootField(
        const graphql::FieldSelection& selection,
        const graphql::FieldSpec& fieldSpec,
        const nlohmann::json& variables) override;

    boost::asio::awaitable<nlohmann::json> resolveSubscriptionField(
        const graphql::FieldSelection& selection,
        const graphql::FieldSpec& fieldSpec,
        const nlohmann::json& variables) override;
};

} // namespace NSNAME
