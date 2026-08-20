#pragma once

#include "name_space.hpp"

#include <expected>
#include <string>

namespace NSNAME::graphql
{

// Human-readable error message carried by all Result failures.
using GraphQLError = std::string;

// Alias for std::expected<T, GraphQLError> used throughout the GraphQL stack.
template <typename T>
using Result = std::expected<T, GraphQLError>;

} // namespace NSNAME::graphql
