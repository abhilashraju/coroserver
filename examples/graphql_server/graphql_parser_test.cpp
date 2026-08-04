#include "graphql/parser.hpp"
#include "graphql/util.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace NSNAME;
using namespace NSNAME::graphql;

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
    Operation operation = Parser::parse(
        "query GetUser($userId: Int!) { user(id: $userId) { id name email age } }");

    expect(operation.type == Operation::Type::Query,
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
    Operation operation = Parser::parse(
        "mutation { createUser(name: \"Test User\", email: \"test@example.com\", age: 25) { id name email age } }");

    expect(operation.type == Operation::Type::Mutation,
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
    Operation operation = Parser::parse(
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
    Operation operation = Parser::parse(
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

// Named fragment: defined after the operation, spread inside the query body.
void testNamedFragmentExpansion()
{
    Operation operation = Parser::parse(
        "query { user { ...UserFields } }"
        " fragment UserFields on User { id name email }");

    // Fragment is stored in the registry
    expect(operation.fragments.count("UserFields") == 1,
           "Expected fragment to be stored in registry");

    // Before expansion the spread is a placeholder
    expect(operation.selections[0].selections.size() == 1,
           "Expected one placeholder before expansion");
    expect(!operation.selections[0].selections[0].fragmentSpreadName.empty(),
           "Expected placeholder to carry fragment name");

    // After expansion the placeholder is replaced by the fragment's fields
    expandFragments(operation);
    expect(operation.selections[0].selections.size() == 3,
           "Expected 3 fields after fragment expansion");
    expect(operation.selections[0].selections[0].name == "id",
           "Expected first expanded field to be 'id'");
    expect(operation.selections[0].selections[1].name == "name",
           "Expected second expanded field to be 'name'");
    expect(operation.selections[0].selections[2].name == "email",
           "Expected third expanded field to be 'email'");
}

// Inline fragment: selection fields must appear directly in the parent set.
void testInlineFragmentExpansion()
{
    Operation operation = Parser::parse(
        "query { user { id ... on User { name email } } }");

    // Inline fragments are expanded at parse time — no placeholders
    expandFragments(operation); // no-op for inline, but must not throw
    expect(operation.selections[0].selections.size() == 3,
           "Expected id + 2 inline fragment fields");
    expect(operation.selections[0].selections[0].name == "id",
           "Expected 'id' field");
    expect(operation.selections[0].selections[1].name == "name",
           "Expected 'name' from inline fragment");
    expect(operation.selections[0].selections[2].name == "email",
           "Expected 'email' from inline fragment");
}

// Circular fragment reference must throw rather than loop forever.
void testCircularFragmentDetection()
{
    Operation operation = Parser::parse(
        "query { a { ...FragA } }"
        " fragment FragA on T { b { ...FragB } }"
        " fragment FragB on T { c { ...FragA } }");

    bool threw = false;
    try
    {
        expandFragments(operation);
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    expect(threw, "Expected circular fragment reference to throw");
}

void testInvalidSyntax()
{
    std::string error;
    expect(!Parser::validate("query { users { id name }", error),
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
        testNamedFragmentExpansion();
        testInlineFragmentExpansion();
        testCircularFragmentDetection();
        testInvalidSyntax();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}
