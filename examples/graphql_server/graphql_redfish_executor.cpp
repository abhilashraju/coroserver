#include "graphql_redfish_executor.hpp"

namespace NSNAME
{

boost::asio::awaitable<nlohmann::json> RedfishGraphQLExecutor::resolveRootField(
    const graphql::FieldSelection& selection,
    const graphql::FieldSpec& fieldSpec, const nlohmann::json& variables)
{
    co_return co_await resolveByPath(selection, fieldSpec, variables,
                                     /*fresh=*/false);
}

boost::asio::awaitable<nlohmann::json>
    RedfishGraphQLExecutor::resolveSubscriptionField(
        const graphql::FieldSelection& selection,
        const graphql::FieldSpec& fieldSpec, const nlohmann::json& variables)
{
    co_return co_await resolveByPath(selection, fieldSpec, variables,
                                     /*fresh=*/true);
}

} // namespace NSNAME
