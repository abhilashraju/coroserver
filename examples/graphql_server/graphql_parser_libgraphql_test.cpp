#include "graphql_parser_libgraphql.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace NSNAME;

namespace
{

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void testQueryWithVariables()
{
    ParsedOperation operation = LibGraphQLParser::parse(
        "query GetUser($userId: Int!) { user(id: $userId) { id name email age } }");

    expect(operation.type == ParsedOperation::OperationType::Query,
           "Expected query operation type");
    expect(operation.name == "GetUser", "Expected named query operation");
    expect(operation.variableDefinitions.size() == 1,
           "Expected variable definition to be captured");
    expect(operation.variableDefinitions[0].name == "userId",
           "Expected variable name to be preserved");
    expect(operation.selections.size() == 1 &&
               operation.selections[0].name == "user",
           "Expected top-level user field");
    expect(operation.selections[0].arguments.size() == 1,
           "Expected field arguments for user");
    expect(operation.selections[0].arguments[0].name == "id",
           "Expected argument name to be preserved");
    expect(operation.selections[0].arguments[0].value.value["$variable"] ==
               "userId",
           "Expected variable reference in argument value");
    expect(operation.selections[0].selections.size() == 4,
           "Expected nested field selection capture");
}

void testMutationLiteralValues()
{
    ParsedOperation operation = LibGraphQLParser::parse(
        "mutation { createUser(name: \"Test User\", email: \"test@example.com\", age: 25) { id name email age } }");

    expect(operation.type == ParsedOperation::OperationType::Mutation,
           "Expected mutation operation type");
    expect(operation.selections.size() == 1,
           "Expected one mutation selection");
    expect(operation.selections[0].arguments.size() == 3,
           "Expected mutation arguments");
    expect(operation.selections[0].arguments[0].value.value == "Test User",
           "Expected string argument parsing");
    expect(operation.selections[0].arguments[2].value.value == 25,
           "Expected integer argument parsing");
}

void testNestedObjectAndListValues()
{
    ParsedOperation operation = LibGraphQLParser::parse(
        "query($input: Input = {enabled: true, ids: [1, 2, 3]}) { users { id } }");

    expect(operation.variableDefinitions.size() == 1,
           "Expected default variable value capture");
    expect(operation.variableDefinitions[0].defaultValue.has_value(),
           "Expected variable default value");
    expect(operation.variableDefinitions[0].defaultValue->value["enabled"] == true,
           "Expected object boolean value parsing");
    expect(operation.variableDefinitions[0].defaultValue->value["ids"].is_array(),
           "Expected list value parsing");
    expect(operation.variableDefinitions[0].defaultValue->value["ids"].size() == 3,
           "Expected full list value capture");
}

void testDirectiveAndFragmentTolerance()
{
    ParsedOperation operation = LibGraphQLParser::parse(
        "query GetUsers @skip(if: false) { users @include(if: true) { id name ... on User { email } } } fragment UserFields on User { id }"
    );

    expect(operation.name == "GetUsers",
           "Expected directive-bearing query to parse");
    expect(operation.directives.size() == 1,
           "Expected operation directive to be captured");
    expect(operation.selections.size() == 1 &&
               operation.selections[0].name == "users",
           "Expected users field to remain intact");
    expect(operation.selections[0].directives.size() == 1,
           "Expected field directive to be captured");
}

void testInvalidSyntax()
{
    std::string error;
    expect(!LibGraphQLParser::validate("query { users { id name }", error),
           "Expected invalid query to fail validation");
    expect(!error.empty(), "Expected validation error message");
}

} // namespace

int main()
{
    try
    {
        testQueryWithVariables();
        testMutationLiteralValues();
        testNestedObjectAndListValues();
        testDirectiveAndFragmentTolerance();
        testInvalidSyntax();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}
