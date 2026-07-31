#include "graphql_parser_libgraphql.hpp"

#include "graphql_handler.hpp"

#include <limits>

namespace NSNAME
{

LibGraphQLParser::Token LibGraphQLParser::Tokenizer::next()
{
    skipIgnored();
    if (atEnd())
    {
        return {TokenType::End, "", position};
    }

    char ch = peek();
    if (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_')
    {
        return readName();
    }
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0 ||
        (ch == '-' && std::isdigit(static_cast<unsigned char>(peek(1))) != 0))
    {
        return readNumber();
    }

    std::size_t start = position;
    ++position;
    switch (ch)
    {
        case '$':
            return {TokenType::Dollar, "$", start};
        case '!':
            return {TokenType::Bang, "!", start};
        case ':':
            return {TokenType::Colon, ":", start};
        case '=':
            return {TokenType::Equals, "=", start};
        case '@':
            return {TokenType::At, "@", start};
        case '(':
            return {TokenType::LeftParen, "(", start};
        case ')':
            return {TokenType::RightParen, ")", start};
        case '{':
            return {TokenType::LeftBrace, "{", start};
        case '}':
            return {TokenType::RightBrace, "}", start};
        case '[':
            return {TokenType::LeftBracket, "[", start};
        case ']':
            return {TokenType::RightBracket, "]", start};
        case ',':
            return {TokenType::Comma, ",", start};
        case '"':
            --position;
            return readString();
        case '.':
            if (peek() == '.' && peek(1) == '.')
            {
                position += 2;
                return {TokenType::Spread, "...", start};
            }
            break;
        default:
            break;
    }

    throw std::runtime_error("Unexpected character '" + std::string(1, ch) +
                             "' at position " + std::to_string(start));
}

LibGraphQLParser::Parser::Parser(std::string_view query) : tokenizer(query)
{
    advance();
}

ParsedOperation LibGraphQLParser::Parser::parseDocument()
{
    if (current.type == TokenType::Spread)
    {
        parseFragmentDefinition();
        if (current.type != TokenType::End)
        {
            throwError("Only one operation is supported per document");
        }
    }

    ParsedOperation operation = parseOperationDefinition();
    while (current.type == TokenType::Spread)
    {
        parseFragmentDefinition();
    }
    if (current.type != TokenType::End)
    {
        throwError("Unexpected token after operation");
    }
    return operation;
}

ParsedOperation LibGraphQLParser::Parser::parseOperationDefinition()
{
    ParsedOperation operation;

    if (current.type == TokenType::LeftBrace)
    {
        operation.type = ParsedOperation::OperationType::Query;
    }
    else if (current.type == TokenType::Name)
    {
        if (current.text == "query")
        {
            operation.type = ParsedOperation::OperationType::Query;
            advance();
        }
        else if (current.text == "mutation")
        {
            operation.type = ParsedOperation::OperationType::Mutation;
            advance();
        }
        else if (current.text == "subscription")
        {
            operation.type = ParsedOperation::OperationType::Subscription;
            advance();
        }
        else
        {
            throwError("Expected operation type or selection set");
        }

        if (current.type == TokenType::Name)
        {
            operation.name = current.text;
            advance();
        }

        if (current.type == TokenType::LeftParen)
        {
            parseVariableDefinitions(operation);
        }

        operation.directives = parseDirectives();
    }
    else
    {
        throwError("Expected operation definition");
    }

    operation.selections = parseSelectionSet();
    return operation;
}

void LibGraphQLParser::Parser::parseVariableDefinitions(
    ParsedOperation& operation)
{
    expect(TokenType::LeftParen, "Expected '(' to start variable definitions");
    while (current.type != TokenType::RightParen)
    {
        GraphQLVariableDefinition definition;
        expect(TokenType::Dollar, "Expected '$' in variable definition");
        definition.name =
            expect(TokenType::Name, "Expected variable name in definition").text;
        expect(TokenType::Colon, "Expected ':' after variable name");
        skipTypeReference(definition);
        if (accept(TokenType::Equals))
        {
            definition.defaultValue = parseValue();
        }
        operation.variableDefinitions.push_back(std::move(definition));
        if (!accept(TokenType::Comma) && current.type != TokenType::RightParen)
        {
            continue;
        }
    }
    expect(TokenType::RightParen, "Expected ')' after variable definitions");
}

std::vector<GraphQLFieldSelection> LibGraphQLParser::Parser::parseSelectionSet()
{
    expect(TokenType::LeftBrace, "Expected '{' to start selection set");
    std::vector<GraphQLFieldSelection> fields;
    while (current.type != TokenType::RightBrace)
    {
        if (current.type == TokenType::Spread)
        {
            parseFragmentSpread();
            continue;
        }
        fields.push_back(parseField());
    }
    expect(TokenType::RightBrace, "Expected '}' to end selection set");
    return fields;
}

GraphQLFieldSelection LibGraphQLParser::Parser::parseField()
{
    GraphQLFieldSelection field;
    std::string fieldName =
        expect(TokenType::Name, "Expected field name in selection set").text;
    field.name = fieldName;

    if (accept(TokenType::Colon))
    {
        field.alias = fieldName;
        field.name =
            expect(TokenType::Name, "Expected field name after alias").text;
    }

    if (current.type == TokenType::LeftParen)
    {
        field.arguments = parseArguments();
    }

    field.directives = parseDirectives();

    if (current.type == TokenType::LeftBrace)
    {
        field.selections = parseSelectionSet();
    }

    return field;
}

std::vector<GraphQLArgument> LibGraphQLParser::Parser::parseArguments()
{
    std::vector<GraphQLArgument> arguments;
    expect(TokenType::LeftParen, "Expected '(' to start arguments");
    while (current.type != TokenType::RightParen)
    {
        GraphQLArgument argument;
        argument.name =
            expect(TokenType::Name, "Expected argument name in field arguments")
                .text;
        expect(TokenType::Colon, "Expected ':' after argument name");
        argument.value = parseValue();
        arguments.push_back(std::move(argument));
        if (!accept(TokenType::Comma) && current.type != TokenType::RightParen)
        {
            continue;
        }
    }
    expect(TokenType::RightParen, "Expected ')' after arguments");
    return arguments;
}

GraphQLDirective LibGraphQLParser::Parser::parseDirective()
{
    GraphQLDirective directive;
    expect(TokenType::At, "Expected '@' to start directive");
    directive.name = expect(TokenType::Name, "Expected directive name").text;
    if (current.type == TokenType::LeftParen)
    {
        directive.arguments = parseArguments();
    }
    return directive;
}

std::vector<GraphQLDirective> LibGraphQLParser::Parser::parseDirectives()
{
    std::vector<GraphQLDirective> directives;
    while (current.type == TokenType::At)
    {
        directives.push_back(parseDirective());
    }
    return directives;
}

GraphQLValue LibGraphQLParser::Parser::parseValue()
{
    if (current.type == TokenType::Dollar)
    {
        advance();
        std::string variableName =
            expect(TokenType::Name, "Expected variable name after '$'").text;
        return {GraphQLValue::Kind::Variable,
                nlohmann::json::object({{"$variable", variableName}})};
    }
    if (current.type == TokenType::String)
    {
        std::string value = current.text;
        advance();
        return {GraphQLValue::Kind::String, value};
    }
    if (current.type == TokenType::Int)
    {
        std::string value = current.text;
        advance();
        try
        {
            long long parsed = std::stoll(value);
            if (parsed >= std::numeric_limits<int>::min() &&
                parsed <= std::numeric_limits<int>::max())
            {
                return {GraphQLValue::Kind::Int, static_cast<int>(parsed)};
            }
            return {GraphQLValue::Kind::Int, parsed};
        }
        catch (...)
        {
            return {GraphQLValue::Kind::Int, value};
        }
    }
    if (current.type == TokenType::Float)
    {
        std::string value = current.text;
        advance();
        try
        {
            return {GraphQLValue::Kind::Float, std::stod(value)};
        }
        catch (...)
        {
            return {GraphQLValue::Kind::Float, value};
        }
    }
    if (current.type == TokenType::LeftBracket)
    {
        return parseListValue();
    }
    if (current.type == TokenType::LeftBrace)
    {
        return parseObjectValue();
    }
    if (current.type == TokenType::Name)
    {
        std::string value = current.text;
        advance();
        if (value == "true")
        {
            return {GraphQLValue::Kind::Boolean, true};
        }
        if (value == "false")
        {
            return {GraphQLValue::Kind::Boolean, false};
        }
        if (value == "null")
        {
            return {GraphQLValue::Kind::Null, nullptr};
        }
        return {GraphQLValue::Kind::Enum, value};
    }

    throwError("Expected value");
}

GraphQLValue LibGraphQLParser::Parser::parseListValue()
{
    nlohmann::json result = nlohmann::json::array();
    expect(TokenType::LeftBracket, "Expected '[' to start list value");
    while (current.type != TokenType::RightBracket)
    {
        result.push_back(parseValue().value);
        if (!accept(TokenType::Comma) && current.type != TokenType::RightBracket)
        {
            continue;
        }
    }
    expect(TokenType::RightBracket, "Expected ']' to end list value");
    return {GraphQLValue::Kind::List, result};
}

GraphQLValue LibGraphQLParser::Parser::parseObjectValue()
{
    nlohmann::json result = nlohmann::json::object();
    expect(TokenType::LeftBrace, "Expected '{' to start object value");
    while (current.type != TokenType::RightBrace)
    {
        std::string fieldName =
            expect(TokenType::Name, "Expected field name in object value").text;
        expect(TokenType::Colon, "Expected ':' after object field name");
        result[fieldName] = parseValue().value;
        if (!accept(TokenType::Comma) && current.type != TokenType::RightBrace)
        {
            continue;
        }
    }
    expect(TokenType::RightBrace, "Expected '}' to end object value");
    return {GraphQLValue::Kind::Object, result};
}

void LibGraphQLParser::Parser::parseFragmentDefinition()
{
    expect(TokenType::Spread, "Expected fragment definition");
    if (isName("on"))
    {
        parseInlineFragment();
        return;
    }

    expect(TokenType::Name, "Expected fragment name");
    if (!isName("on"))
    {
        throwError("Expected 'on' in fragment definition");
    }
    advance();
    expect(TokenType::Name, "Expected type condition in fragment definition");
    parseDirectives();
    parseSelectionSet();
}

void LibGraphQLParser::Parser::parseFragmentSpread()
{
    expect(TokenType::Spread, "Expected fragment spread");
    if (isName("on"))
    {
        parseInlineFragment();
        return;
    }
    expect(TokenType::Name, "Expected fragment name after '...'");
    parseDirectives();
}

void LibGraphQLParser::Parser::parseInlineFragment()
{
    if (isName("on"))
    {
        advance();
        expect(TokenType::Name, "Expected inline fragment type condition");
    }
    parseDirectives();
    parseSelectionSet();
}

void LibGraphQLParser::Parser::skipTypeReference(
    GraphQLVariableDefinition& definition)
{
    if (accept(TokenType::LeftBracket))
    {
        definition.isList = true;
        Token typeName =
            expect(TokenType::Name, "Expected type name in variable definition");
        definition.typeName = typeName.text;
        expect(TokenType::RightBracket, "Expected ']' in type reference");
    }
    else
    {
        definition.typeName =
            expect(TokenType::Name, "Expected type name in variable definition")
                .text;
    }

    definition.nonNull = accept(TokenType::Bang);
}

bool LibGraphQLParser::Parser::isName(const char* text) const
{
    return current.type == TokenType::Name && current.text == text;
}

LibGraphQLParser::Token LibGraphQLParser::Parser::expect(
    TokenType type, const char* message)
{
    if (current.type != type)
    {
        throwError(message);
    }
    Token token = current;
    advance();
    return token;
}

bool LibGraphQLParser::Parser::accept(TokenType type)
{
    if (current.type != type)
    {
        return false;
    }
    advance();
    return true;
}

void LibGraphQLParser::Parser::advance()
{
    current = tokenizer.next();
}

[[noreturn]] void LibGraphQLParser::Parser::throwError(
    const std::string& message) const
{
    throw std::runtime_error(message + " at position " +
                             std::to_string(current.position));
}

ParsedOperation LibGraphQLParser::parse(const std::string& query)
{
    Parser parser(query);
    return parser.parseDocument();
}

bool LibGraphQLParser::validate(const std::string& query, std::string& errorMsg)
{
    try
    {
        parse(query);
        errorMsg.clear();
        return true;
    }
    catch (const std::exception& e)
    {
        errorMsg = e.what();
        return false;
    }
}

static nlohmann::json resolveVariables(const nlohmann::json& value,
                                       const nlohmann::json& variables)
{
    if (value.is_object())
    {
        if (value.contains("$variable") && value.size() == 1)
        {
            const std::string& varName = value["$variable"].get<std::string>();
            if (variables.contains(varName))
            {
                return variables[varName];
            }
            return nullptr;
        }

        nlohmann::json result = nlohmann::json::object();
        for (auto it = value.begin(); it != value.end(); ++it)
        {
            result[it.key()] = resolveVariables(it.value(), variables);
        }
        return result;
    }
    if (value.is_array())
    {
        nlohmann::json result = nlohmann::json::array();
        for (const auto& item : value)
        {
            result.push_back(resolveVariables(item, variables));
        }
        return result;
    }
    return value;
}

static nlohmann::json argumentsToJson(
    const std::vector<GraphQLArgument>& arguments, const nlohmann::json& variables)
{
    nlohmann::json result = nlohmann::json::object();
    for (const GraphQLArgument& argument : arguments)
    {
        result[argument.name] = resolveVariables(argument.value.value, variables);
    }
    return result;
}

static std::vector<std::string> selectionNames(
    const std::vector<GraphQLFieldSelection>& selections)
{
    std::vector<std::string> result;
    result.reserve(selections.size());
    for (const GraphQLFieldSelection& selection : selections)
    {
        result.push_back(selection.alias.empty() ? selection.name : selection.alias);
    }
    return result;
}

static nlohmann::json filterFields(
    const nlohmann::json& data, const std::vector<GraphQLFieldSelection>& selections)
{
    if (selections.empty())
    {
        return data;
    }
    if (data.is_array())
    {
        nlohmann::json result = nlohmann::json::array();
        for (const auto& item : data)
        {
            result.push_back(filterFields(item, selections));
        }
        return result;
    }
    if (data.is_object())
    {
        nlohmann::json result = nlohmann::json::object();
        for (const GraphQLFieldSelection& selection : selections)
        {
            const std::string& sourceName = selection.name;
            std::string outputName =
                selection.alias.empty() ? selection.name : selection.alias;
            if (!data.contains(sourceName))
            {
                continue;
            }
            if (selection.selections.empty())
            {
                result[outputName] = data[sourceName];
            }
            else
            {
                result[outputName] =
                    filterFields(data[sourceName], selection.selections);
            }
        }
        return result;
    }
    return data;
}

static nlohmann::json initialVariables(const ParsedOperation& operation)
{
    nlohmann::json variables = nlohmann::json::object();
    for (const GraphQLVariableDefinition& definition : operation.variableDefinitions)
    {
        variables[definition.name] = definition.defaultValue
                                         ? definition.defaultValue->value
                                         : nlohmann::json(nullptr);
    }
    return variables;
}

boost::asio::awaitable<nlohmann::json> LibGraphQLExecutor::execute(
    const std::string& query, const nlohmann::json& variables)
{
    nlohmann::json response;

    try
    {
        std::string errorMsg;
        if (!LibGraphQLParser::validate(query, errorMsg))
        {
            response["errors"] =
                nlohmann::json::array({{{"message", errorMsg}}});
            co_return response;
        }

        auto operation = LibGraphQLParser::parse(query);

        nlohmann::json mergedVariables = initialVariables(operation);
        for (auto it = variables.begin(); it != variables.end(); ++it)
        {
            mergedVariables[it.key()] = it.value();
        }

        nlohmann::json data;
        switch (operation.type)
        {
            case ParsedOperation::OperationType::Query:
                data = co_await executeQuery(operation, mergedVariables);
                break;
            case ParsedOperation::OperationType::Mutation:
                data = co_await executeMutation(operation, mergedVariables);
                break;
            case ParsedOperation::OperationType::Subscription:
                throw std::runtime_error("Subscriptions are not yet supported");
            default:
                throw std::runtime_error("Unknown operation type");
        }

        response["data"] = data;
    }
    catch (const std::exception& e)
    {
        response["errors"] = nlohmann::json::array({{{"message", e.what()}}});
    }

    co_return response;
}

boost::asio::awaitable<nlohmann::json> LibGraphQLExecutor::executeQuery(
    const ParsedOperation& operation, const nlohmann::json& variables)
{
    nlohmann::json result;

    for (const GraphQLFieldSelection& field : operation.selections)
    {
        auto resolver = schema_->getQuery(field.name);
        if (resolver)
        {
            nlohmann::json fieldArgs = variables;
            nlohmann::json parsedArgs = argumentsToJson(field.arguments, variables);
            for (auto it = parsedArgs.begin(); it != parsedArgs.end(); ++it)
            {
                fieldArgs[it.key()] = it.value();
            }

            nlohmann::json fieldResult =
                co_await (*resolver)(fieldArgs, nlohmann::json::object());
            fieldResult = filterFields(fieldResult, field.selections);

            std::string outputName =
                field.alias.empty() ? field.name : field.alias;
            result[outputName] = fieldResult;
        }
        else
        {
            throw std::runtime_error("Unknown query field: " + field.name);
        }
    }

    co_return result;
}

boost::asio::awaitable<nlohmann::json> LibGraphQLExecutor::executeMutation(
    const ParsedOperation& operation, const nlohmann::json& variables)
{
    nlohmann::json result;

    for (const GraphQLFieldSelection& field : operation.selections)
    {
        auto resolver = schema_->getMutation(field.name);
        if (resolver)
        {
            nlohmann::json fieldArgs = variables;
            nlohmann::json parsedArgs = argumentsToJson(field.arguments, variables);
            for (auto it = parsedArgs.begin(); it != parsedArgs.end(); ++it)
            {
                fieldArgs[it.key()] = it.value();
            }

            nlohmann::json fieldResult =
                co_await (*resolver)(fieldArgs, nlohmann::json::object());
            fieldResult = filterFields(fieldResult, field.selections);

            std::string outputName =
                field.alias.empty() ? field.name : field.alias;
            result[outputName] = fieldResult;
        }
        else
        {
            throw std::runtime_error("Unknown mutation field: " + field.name);
        }
    }

    co_return result;
}

} // namespace NSNAME
