#pragma once

#include "name_space.hpp"

#include <graphqlparser/Ast.h>
#include <graphqlparser/AstVisitor.h>
#include <graphqlparser/GraphQLParser.h>
#include <graphqlparser/c/GraphQLAst.h>
#include <graphqlparser/c/GraphQLAstNode.h>
#include <graphqlparser/c/GraphQLAstVisitor.h>
#include <graphqlparser/c/GraphQLParser.h>

#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace NSNAME
{

// Forward declarations
class GraphQLSchema;

// Parsed GraphQL operation
struct ParsedOperation
{
    enum class OperationType
    {
        Query,
        Mutation,
        Subscription
    };

    OperationType type;
    std::string name;
    std::vector<std::string> selectionSet;
    nlohmann::json variables;
    std::unordered_map<std::string, nlohmann::json> fieldArguments;
    // Map of field name to its requested sub-fields
    std::unordered_map<std::string, std::vector<std::string>> fieldSelections;
};

// AST Visitor for extracting operation information using visitor pattern
class GraphQLASTVisitor
{
  public:
    explicit GraphQLASTVisitor(const char* error = nullptr) : parseError(error)
    {}

    ParsedOperation extractOperation(const struct GraphQLAstDocument* document)
    {
        if (parseError)
        {
            throw std::runtime_error(std::string("Parse error: ") + parseError);
        }

        if (!document)
        {
            throw std::runtime_error("Invalid document");
        }

        // Use visitor pattern to traverse the AST
        GraphQLAstVisitorCallbacks callbacks = {};
        callbacks.visit_operation_definition = visitOperationDefinition;
        callbacks.visit_selection_set = visitSelectionSet;
        callbacks.visit_field = visitField;
        callbacks.visit_argument = visitArgument;
        callbacks.visit_variable_definition = visitVariableDefinition;

        graphql_node_visit(
            reinterpret_cast<const struct GraphQLAstNode*>(document),
            &callbacks, this);

        if (operations.empty())
        {
            throw std::runtime_error("No operation found in document");
        }

        return operations[0];
    }

  private:
    static int visitOperationDefinition(
        const struct GraphQLAstOperationDefinition* opDef, void* userData)
    {
        auto* visitor = static_cast<GraphQLASTVisitor*>(userData);
        ParsedOperation operation;

        // Get operation type
        const char* opTypeStr =
            GraphQLAstOperationDefinition_get_operation(opDef);
        if (opTypeStr)
        {
            std::string opType(opTypeStr);
            if (opType == "query")
            {
                operation.type = ParsedOperation::OperationType::Query;
            }
            else if (opType == "mutation")
            {
                operation.type = ParsedOperation::OperationType::Mutation;
            }
            else if (opType == "subscription")
            {
                operation.type = ParsedOperation::OperationType::Subscription;
            }
        }
        else
        {
            operation.type = ParsedOperation::OperationType::Query;
        }

        // Get operation name
        const struct GraphQLAstName* nameNode =
            GraphQLAstOperationDefinition_get_name(opDef);
        if (nameNode)
        {
            const char* name = GraphQLAstName_get_value(nameNode);
            if (name)
            {
                operation.name = name;
            }
        }

        visitor->operations.push_back(operation);
        visitor->currentOperation = &visitor->operations.back();
        visitor->fieldDepth = 0; // Reset depth for new operation
        return 1;                // Continue visiting children
    }

    static int visitSelectionSet(
        const struct GraphQLAstSelectionSet* selectionSet, void* userData)
    {
        auto* visitor = static_cast<GraphQLASTVisitor*>(userData);
        visitor->fieldDepth++;
        return 1; // Continue visiting children
    }

    static int visitField(const struct GraphQLAstField* field, void* userData)
    {
        auto* visitor = static_cast<GraphQLASTVisitor*>(userData);
        if (!visitor->currentOperation)
            return 1;

        const struct GraphQLAstName* fieldName =
            GraphQLAstField_get_name(field);
        if (fieldName)
        {
            const char* name = GraphQLAstName_get_value(fieldName);
            if (name)
            {
                // Only add top-level fields (depth == 1) to the selection set
                // depth == 1 means it's directly under the operation's
                // selection set
                if (visitor->fieldDepth == 1)
                {
                    visitor->currentFieldName = name;
                    visitor->currentOperation->selectionSet.push_back(name);
                }
                // Capture nested fields (depth == 2) as sub-selections of the
                // parent field
                else if (visitor->fieldDepth == 2 &&
                         !visitor->currentFieldName.empty())
                {
                    visitor->currentOperation
                        ->fieldSelections[visitor->currentFieldName]
                        .push_back(name);
                }
            }
        }
        return 1; // Continue visiting children
    }

    static int visitArgument(const struct GraphQLAstArgument* arg,
                             void* userData)
    {
        auto* visitor = static_cast<GraphQLASTVisitor*>(userData);
        if (!visitor->currentOperation || visitor->currentFieldName.empty())
            return 1;

        const struct GraphQLAstName* argName = GraphQLAstArgument_get_name(arg);
        const struct GraphQLAstValue* argValue =
            GraphQLAstArgument_get_value(arg);

        if (argName && argValue)
        {
            const char* argNameStr = GraphQLAstName_get_value(argName);
            if (argNameStr)
            {
                if (visitor->currentOperation->fieldArguments.find(
                        visitor->currentFieldName) ==
                    visitor->currentOperation->fieldArguments.end())
                {
                    visitor->currentOperation
                        ->fieldArguments[visitor->currentFieldName] =
                        nlohmann::json::object();
                }
                visitor->currentOperation
                    ->fieldArguments[visitor->currentFieldName][argNameStr] =
                    visitor->extractValue(argValue);
            }
        }
        return 1;
    }

    static int visitVariableDefinition(
        const struct GraphQLAstVariableDefinition* varDef, void* userData)
    {
        auto* visitor = static_cast<GraphQLASTVisitor*>(userData);
        if (!visitor->currentOperation)
            return 1;

        const struct GraphQLAstVariable* var =
            GraphQLAstVariableDefinition_get_variable(varDef);
        if (var)
        {
            const struct GraphQLAstName* varName =
                GraphQLAstVariable_get_name(var);
            if (varName)
            {
                const char* name = GraphQLAstName_get_value(varName);
                if (name)
                {
                    visitor->currentOperation->variables[name] = nullptr;
                }
            }
        }
        return 1;
    }

    nlohmann::json extractValue(const struct GraphQLAstValue* value)
    {
        if (!value)
            return nullptr;

        // Use visitor pattern to properly identify and extract the value type
        struct ValueContext
        {
            nlohmann::json result;
            bool found = false;
            GraphQLASTVisitor* visitor;
        };
        ValueContext ctx{nullptr, false, this};

        GraphQLAstVisitorCallbacks callbacks = {};

        // Handle IntValue
        callbacks.visit_int_value =
            [](const struct GraphQLAstIntValue* iv, void* userData) -> int {
            auto* context = static_cast<ValueContext*>(userData);
            const char* val = GraphQLAstIntValue_get_value(iv);
            if (val)
            {
                try
                {
                    // Try to parse as int first for better compatibility
                    long long llval = std::stoll(val);
                    // Check if it fits in an int
                    if (llval >= std::numeric_limits<int>::min() &&
                        llval <= std::numeric_limits<int>::max())
                    {
                        context->result = static_cast<int>(llval);
                    }
                    else
                    {
                        context->result = llval;
                    }
                    context->found = true;
                }
                catch (...)
                {
                    context->result = std::string(val);
                    context->found = true;
                }
            }
            return 0; // Don't recurse
        };

        // Handle FloatValue
        callbacks.visit_float_value =
            [](const struct GraphQLAstFloatValue* fv, void* userData) -> int {
            auto* context = static_cast<ValueContext*>(userData);
            const char* val = GraphQLAstFloatValue_get_value(fv);
            if (val)
            {
                try
                {
                    context->result = std::stod(val);
                    context->found = true;
                }
                catch (...)
                {
                    context->result = std::string(val);
                    context->found = true;
                }
            }
            return 0;
        };

        // Handle StringValue
        callbacks.visit_string_value =
            [](const struct GraphQLAstStringValue* sv, void* userData) -> int {
            auto* context = static_cast<ValueContext*>(userData);
            const char* val = GraphQLAstStringValue_get_value(sv);
            if (val)
            {
                context->result = std::string(val);
                context->found = true;
            }
            return 0;
        };

        // Handle BooleanValue
        callbacks.visit_boolean_value =
            [](const struct GraphQLAstBooleanValue* bv, void* userData) -> int {
            auto* context = static_cast<ValueContext*>(userData);
            int val = GraphQLAstBooleanValue_get_value(bv);
            context->result = static_cast<bool>(val);
            context->found = true;
            return 0;
        };

        // Handle NullValue
        callbacks.visit_null_value =
            [](const struct GraphQLAstNullValue*, void* userData) -> int {
            auto* context = static_cast<ValueContext*>(userData);
            context->result = nullptr;
            context->found = true;
            return 0;
        };

        // Handle EnumValue
        callbacks.visit_enum_value =
            [](const struct GraphQLAstEnumValue* ev, void* userData) -> int {
            auto* context = static_cast<ValueContext*>(userData);
            const char* val = GraphQLAstEnumValue_get_value(ev);
            if (val)
            {
                context->result = std::string(val);
                context->found = true;
            }
            return 0;
        };

        // Handle ListValue
        callbacks.visit_list_value =
            [](const struct GraphQLAstListValue* lv, void* userData) -> int {
            auto* context = static_cast<ValueContext*>(userData);
            context->result = nlohmann::json::array();
            context->found = true;
            return 1; // Continue to visit children
        };

        // For list items, we need to recursively extract values
        callbacks.end_visit_list_value =
            [](const struct GraphQLAstListValue* lv, void* userData) {
                // List items are handled by recursive calls in visit callbacks
            };

        // Handle ObjectValue
        callbacks.visit_object_value =
            [](const struct GraphQLAstObjectValue* ov, void* userData) -> int {
            auto* context = static_cast<ValueContext*>(userData);
            context->result = nlohmann::json::object();
            context->found = true;
            return 1; // Continue to visit children (object fields)
        };

        // Handle ObjectField
        callbacks.visit_object_field =
            [](const struct GraphQLAstObjectField* of, void* userData) -> int {
            auto* context = static_cast<ValueContext*>(userData);
            const struct GraphQLAstName* name =
                GraphQLAstObjectField_get_name(of);
            if (name)
            {
                const char* nameStr = GraphQLAstName_get_value(name);
                if (nameStr)
                {
                    const struct GraphQLAstValue* fieldValue =
                        GraphQLAstObjectField_get_value(of);
                    if (fieldValue)
                    {
                        context->result[nameStr] =
                            context->visitor->extractValue(fieldValue);
                    }
                }
            }
            return 0; // Don't recurse (we handle value extraction above)
        };

        // Handle Variable
        callbacks.visit_variable =
            [](const struct GraphQLAstVariable* var, void* userData) -> int {
            auto* context = static_cast<ValueContext*>(userData);
            const struct GraphQLAstName* varName =
                GraphQLAstVariable_get_name(var);
            if (varName)
            {
                const char* name = GraphQLAstName_get_value(varName);
                if (name)
                {
                    // Return a special marker for variables
                    context->result =
                        nlohmann::json::object({{"$variable", name}});
                    context->found = true;
                }
            }
            return 0;
        };

        // Visit the value node
        graphql_node_visit(
            reinterpret_cast<const struct GraphQLAstNode*>(value), &callbacks,
            &ctx);

        return ctx.found ? ctx.result : nullptr;
    }

    const char* parseError;
    std::vector<ParsedOperation> operations;
    ParsedOperation* currentOperation = nullptr;
    std::string currentFieldName;
    int fieldDepth = 0;
};

// GraphQL Parser using libgraphqlparser
class LibGraphQLParser
{
  public:
    static ParsedOperation parse(const std::string& query)
    {
        const char* error = nullptr;

        // Parse the GraphQL query
        struct GraphQLAstNode* node =
            graphql_parse_string(query.c_str(), &error);

        if (!node)
        {
            std::string errorMsg = error ? error : "Unknown parse error";
            graphql_error_free(error);
            throw std::runtime_error("GraphQL parse error: " + errorMsg);
        }

        // Create document from node
        const struct GraphQLAstDocument* document =
            reinterpret_cast<const struct GraphQLAstDocument*>(node);

        // Extract operation using visitor
        GraphQLASTVisitor visitor(error);
        ParsedOperation operation;

        try
        {
            operation = visitor.extractOperation(document);
        }
        catch (...)
        {
            graphql_node_free(node);
            if (error)
            {
                graphql_error_free(error);
            }
            throw;
        }

        // Clean up
        graphql_node_free(node);
        if (error)
        {
            graphql_error_free(error);
        }

        return operation;
    }

    static bool validate(const std::string& query, std::string& errorMsg)
    {
        const char* error = nullptr;
        struct GraphQLAstNode* node =
            graphql_parse_string(query.c_str(), &error);

        if (!node)
        {
            errorMsg = error ? error : "Unknown parse error";
            if (error)
            {
                graphql_error_free(error);
            }
            return false;
        }

        graphql_node_free(node);
        if (error)
        {
            graphql_error_free(error);
        }
        return true;
    }
};

// GraphQL Executor using libgraphqlparser
class LibGraphQLExecutor
{
  public:
    explicit LibGraphQLExecutor(std::shared_ptr<GraphQLSchema> schema) :
        schema_(std::move(schema))
    {}

    boost::asio::awaitable<nlohmann::json> execute(
        const std::string& query,
        const nlohmann::json& variables = nlohmann::json::object());

  private:
    boost::asio::awaitable<nlohmann::json> executeQuery(
        const ParsedOperation& operation, const nlohmann::json& variables);
    boost::asio::awaitable<nlohmann::json> executeMutation(
        const ParsedOperation& operation, const nlohmann::json& variables);

    std::shared_ptr<GraphQLSchema> schema_;
};

} // namespace NSNAME

// Made with Bob
