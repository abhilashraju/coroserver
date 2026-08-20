#pragma once

#include "graphql/error.hpp"
#include "graphql/typed_schema.hpp"

namespace NSNAME
{

// Builds and returns the Redfish TypedSchema.
// Returns std::unexpected if the on-disk JSON file exists but cannot be loaded.
NSNAME::graphql::Result<NSNAME::graphql::TypedSchema> buildRedfishTypedSchema();

} // namespace NSNAME
