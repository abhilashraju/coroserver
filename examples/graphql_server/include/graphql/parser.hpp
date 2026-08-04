#pragma once

#include "graphql/ast.hpp"

#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NSNAME::graphql
{

class Parser
{
  public:
    static Operation parse(const std::string& query)
    {
        Impl impl(query);
        return impl.parseDocument();
    }

    static bool validate(const std::string& query, std::string& errorMsg)
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

  private:
    enum class TokenType
    {
        End,
        Name,
        Int,
        Float,
        String,
        Dollar,
        Bang,
        Colon,
        Equals,
        At,
        Spread,
        LeftParen,
        RightParen,
        LeftBrace,
        RightBrace,
        LeftBracket,
        RightBracket,
        Comma
    };

    struct Token
    {
        TokenType type = TokenType::End;
        std::string text;
        std::size_t position = 0;
    };

    class Tokenizer
    {
      public:
        explicit Tokenizer(std::string_view input) : input(input) {}

        Token next()
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
                (ch == '-' &&
                 std::isdigit(static_cast<unsigned char>(peek(1))) != 0))
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

            throw std::runtime_error("Unexpected character '" +
                                     std::string(1, ch) + "' at position " +
                                     std::to_string(start));
        }

      private:
        bool atEnd() const
        {
            return position >= input.size();
        }

        char peek(std::size_t offset = 0) const
        {
            std::size_t index = position + offset;
            if (index >= input.size())
            {
                return '\0';
            }
            return input[index];
        }

        bool match(char expected)
        {
            if (peek() != expected)
            {
                return false;
            }
            ++position;
            return true;
        }

        void skipIgnored()
        {
            while (!atEnd())
            {
                char ch = peek();
                if (std::isspace(static_cast<unsigned char>(ch)) != 0 ||
                    ch == ',')
                {
                    ++position;
                    continue;
                }
                if (ch == '#')
                {
                    while (!atEnd() && peek() != '\n' && peek() != '\r')
                    {
                        ++position;
                    }
                    continue;
                }
                break;
            }
        }

        Token readName()
        {
            std::size_t start = position;
            ++position;
            while (!atEnd())
            {
                char ch = peek();
                if (std::isalnum(static_cast<unsigned char>(ch)) == 0 &&
                    ch != '_')
                {
                    break;
                }
                ++position;
            }
            return {TokenType::Name,
                    std::string(input.substr(start, position - start)), start};
        }

        Token readNumber()
        {
            std::size_t start = position;
            match('-');
            while (std::isdigit(static_cast<unsigned char>(peek())) != 0)
            {
                ++position;
            }

            TokenType type = TokenType::Int;
            if (peek() == '.' &&
                std::isdigit(static_cast<unsigned char>(peek(1))) != 0)
            {
                type = TokenType::Float;
                ++position;
                while (std::isdigit(static_cast<unsigned char>(peek())) != 0)
                {
                    ++position;
                }
            }

            if (peek() == 'e' || peek() == 'E')
            {
                type = TokenType::Float;
                ++position;
                if (peek() == '+' || peek() == '-')
                {
                    ++position;
                }
                while (std::isdigit(static_cast<unsigned char>(peek())) != 0)
                {
                    ++position;
                }
            }

            return {type, std::string(input.substr(start, position - start)),
                    start};
        }

        Token readString()
        {
            std::size_t start = position;
            ++position;
            std::string result;

            while (!atEnd())
            {
                char ch = peek();
                ++position;
                if (ch == '"')
                {
                    return {TokenType::String, result, start};
                }
                if (ch == '\\')
                {
                    if (atEnd())
                    {
                        throw std::runtime_error(
                            "Unterminated escape sequence at position " +
                            std::to_string(position));
                    }
                    char escaped = peek();
                    ++position;
                    switch (escaped)
                    {
                        case '"':
                        case '\\':
                        case '/':
                            result.push_back(escaped);
                            break;
                        case 'b':
                            result.push_back('\b');
                            break;
                        case 'f':
                            result.push_back('\f');
                            break;
                        case 'n':
                            result.push_back('\n');
                            break;
                        case 'r':
                            result.push_back('\r');
                            break;
                        case 't':
                            result.push_back('\t');
                            break;
                        default:
                            result.push_back(escaped);
                            break;
                    }
                    continue;
                }
                result.push_back(ch);
            }

            throw std::runtime_error("Unterminated string at position " +
                                     std::to_string(start));
        }

        std::string_view input;
        std::size_t position = 0;
    };

    class Impl
    {
      public:
        static constexpr std::size_t kMaxDepth = 20;

        explicit Impl(std::string_view query) : tokenizer(query)
        {
            advance();
        }

        Operation parseDocument()
        {
            Operation operation = parseOperationDefinition();
            while (current.type == TokenType::Name && current.text == "fragment")
            {
                FragmentDefinition frag = parseFragmentDefinition();
                operation.fragments[frag.name] = std::move(frag);
            }
            if (current.type != TokenType::End)
            {
                throwError("Unexpected token after operation");
            }
            return operation;
        }

      private:
        Operation parseOperationDefinition()
        {
            Operation operation;

            if (current.type == TokenType::LeftBrace)
            {
                operation.type = Operation::Type::Query;
            }
            else if (current.type == TokenType::Name)
            {
                if (current.text == "query")
                {
                    operation.type = Operation::Type::Query;
                    advance();
                }
                else if (current.text == "mutation")
                {
                    operation.type = Operation::Type::Mutation;
                    advance();
                }
                else if (current.text == "subscription")
                {
                    operation.type = Operation::Type::Subscription;
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

        void parseVariableDefinitions(Operation& operation)
        {
            expect(TokenType::LeftParen,
                   "Expected '(' to start variable definitions");
            while (current.type != TokenType::RightParen)
            {
                VariableDefinition definition;
                expect(TokenType::Dollar, "Expected '$' in variable definition");
                definition.name =
                    expect(TokenType::Name,
                           "Expected variable name in definition")
                        .text;
                expect(TokenType::Colon, "Expected ':' after variable name");
                skipTypeReference(definition);
                if (accept(TokenType::Equals))
                {
                    definition.defaultValue = parseValue();
                }
                operation.variableDefinitions.push_back(std::move(definition));
                if (!accept(TokenType::Comma) &&
                    current.type != TokenType::RightParen)
                {
                    continue;
                }
            }
            expect(TokenType::RightParen,
                   "Expected ')' after variable definitions");
        }

        std::vector<FieldSelection> parseSelectionSet(
            std::size_t depth = 0)
        {
            if (depth >= kMaxDepth)
            {
                throwError("Query exceeds maximum nesting depth of " +
                           std::to_string(kMaxDepth));
            }
            expect(TokenType::LeftBrace, "Expected '{' to start selection set");
            std::vector<FieldSelection> fields;
            while (current.type != TokenType::RightBrace)
            {
                if (current.type == TokenType::Spread)
                {
                    auto spread = parseFragmentSpread(depth);
                    for (auto& f : spread)
                    {
                        fields.push_back(std::move(f));
                    }
                    continue;
                }
                fields.push_back(parseField(depth));
            }
            expect(TokenType::RightBrace, "Expected '}' to end selection set");
            return fields;
        }

        FieldSelection parseField(std::size_t depth)
        {
            FieldSelection field;
            std::string fieldName =
                expect(TokenType::Name,
                       "Expected field name in selection set")
                    .text;
            field.name = fieldName;

            if (accept(TokenType::Colon))
            {
                field.alias = fieldName;
                field.name =
                    expect(TokenType::Name,
                           "Expected field name after alias")
                        .text;
            }

            if (current.type == TokenType::LeftParen)
            {
                field.arguments = parseArguments();
            }

            field.directives = parseDirectives();

            if (current.type == TokenType::LeftBrace)
            {
                field.selections = parseSelectionSet(depth + 1);
            }

            return field;
        }

        std::vector<Argument> parseArguments()
        {
            std::vector<Argument> arguments;
            expect(TokenType::LeftParen, "Expected '(' to start arguments");
            while (current.type != TokenType::RightParen)
            {
                Argument argument;
                argument.name =
                    expect(TokenType::Name,
                           "Expected argument name in field arguments")
                        .text;
                expect(TokenType::Colon, "Expected ':' after argument name");
                argument.value = parseValue();
                arguments.push_back(std::move(argument));
                if (!accept(TokenType::Comma) &&
                    current.type != TokenType::RightParen)
                {
                    continue;
                }
            }
            expect(TokenType::RightParen, "Expected ')' after arguments");
            return arguments;
        }

        Directive parseDirective()
        {
            Directive directive;
            expect(TokenType::At, "Expected '@' to start directive");
            directive.name =
                expect(TokenType::Name, "Expected directive name").text;
            if (current.type == TokenType::LeftParen)
            {
                directive.arguments = parseArguments();
            }
            return directive;
        }

        std::vector<Directive> parseDirectives()
        {
            std::vector<Directive> directives;
            while (current.type == TokenType::At)
            {
                directives.push_back(parseDirective());
            }
            return directives;
        }

        Value parseValue()
        {
            if (current.type == TokenType::Dollar)
            {
                advance();
                std::string variableName =
                    expect(TokenType::Name,
                           "Expected variable name after '$'")
                        .text;
                return {Value::Kind::Variable,
                        nlohmann::json::object({{"$variable", variableName}})};
            }
            if (current.type == TokenType::String)
            {
                std::string value = current.text;
                advance();
                return {Value::Kind::String, value};
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
                        return {Value::Kind::Int, static_cast<int>(parsed)};
                    }
                    return {Value::Kind::Int, parsed};
                }
                catch (...)
                {
                    return {Value::Kind::Int, value};
                }
            }
            if (current.type == TokenType::Float)
            {
                std::string value = current.text;
                advance();
                try
                {
                    return {Value::Kind::Float, std::stod(value)};
                }
                catch (...)
                {
                    return {Value::Kind::Float, value};
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
                    return {Value::Kind::Boolean, true};
                }
                if (value == "false")
                {
                    return {Value::Kind::Boolean, false};
                }
                if (value == "null")
                {
                    return {Value::Kind::Null, nullptr};
                }
                return {Value::Kind::Enum, value};
            }

            throwError("Expected value");
        }

        Value parseListValue()
        {
            nlohmann::json result = nlohmann::json::array();
            expect(TokenType::LeftBracket, "Expected '[' to start list value");
            while (current.type != TokenType::RightBracket)
            {
                result.push_back(parseValue().value);
                if (!accept(TokenType::Comma) &&
                    current.type != TokenType::RightBracket)
                {
                    continue;
                }
            }
            expect(TokenType::RightBracket, "Expected ']' to end list value");
            return {Value::Kind::List, result};
        }

        Value parseObjectValue()
        {
            nlohmann::json result = nlohmann::json::object();
            expect(TokenType::LeftBrace,
                   "Expected '{' to start object value");
            while (current.type != TokenType::RightBrace)
            {
                std::string fieldName =
                    expect(TokenType::Name,
                           "Expected field name in object value")
                        .text;
                expect(TokenType::Colon,
                       "Expected ':' after object field name");
                result[fieldName] = parseValue().value;
                if (!accept(TokenType::Comma) &&
                    current.type != TokenType::RightBrace)
                {
                    continue;
                }
            }
            expect(TokenType::RightBrace, "Expected '}' to end object value");
            return {Value::Kind::Object, result};
        }

        // Parses a top-level "fragment Foo on Type { ... }" definition.
        FragmentDefinition parseFragmentDefinition()
        {
            expect(TokenType::Name, "Expected 'fragment' keyword");
            FragmentDefinition frag;
            frag.name =
                expect(TokenType::Name, "Expected fragment name").text;
            if (!isName("on"))
            {
                throwError("Expected 'on' in fragment definition");
            }
            advance(); // consume "on"
            frag.typeCondition =
                expect(TokenType::Name,
                       "Expected type condition in fragment definition")
                    .text;
            frag.directives = parseDirectives();
            // Fragment bodies are parsed at depth 0; their actual depth is
            // enforced when they are expanded at the call-site (expandFragments).
            frag.selections = parseSelectionSet(0);
            return frag;
        }

        // Parses a "...FragmentName" or "... on Type { }" spread inside a
        // selection set.  Returns the resulting FieldSelection entries so the
        // caller can merge them directly into the parent selection set.
        std::vector<FieldSelection> parseFragmentSpread(std::size_t depth)
        {
            expect(TokenType::Spread, "Expected '...'");
            if (isName("on"))
            {
                // Inline fragment — expand its selections immediately.
                return parseInlineFragment(depth);
            }
            // Named fragment spread — record the name; expansion happens
            // after the full document is parsed (in expandFragments).
            FieldSelection placeholder;
            placeholder.fragmentSpreadName =
                expect(TokenType::Name,
                       "Expected fragment name after '...'")
                    .text;
            parseDirectives(); // consume any directives (stored in placeholder if needed)
            return {std::move(placeholder)};
        }

        // Parses "... on Type { }" or "... { }" and returns its selections.
        std::vector<FieldSelection> parseInlineFragment(std::size_t depth)
        {
            if (isName("on"))
            {
                advance(); // consume "on"
                expect(TokenType::Name,
                       "Expected inline fragment type condition");
                // type condition is noted but not enforced by this executor
            }
            parseDirectives();
            return parseSelectionSet(depth);
        }

        void skipTypeReference(VariableDefinition& definition)
        {
            if (accept(TokenType::LeftBracket))
            {
                definition.isList = true;
                Token typeName = expect(
                    TokenType::Name,
                    "Expected type name in variable definition");
                definition.typeName = typeName.text;
                expect(TokenType::RightBracket,
                       "Expected ']' in type reference");
            }
            else
            {
                definition.typeName =
                    expect(TokenType::Name,
                           "Expected type name in variable definition")
                        .text;
            }

            definition.nonNull = accept(TokenType::Bang);
        }

        bool isName(const char* text) const
        {
            return current.type == TokenType::Name && current.text == text;
        }

        Token expect(TokenType type, const char* message)
        {
            if (current.type != type)
            {
                throwError(message);
            }
            Token token = current;
            advance();
            return token;
        }

        bool accept(TokenType type)
        {
            if (current.type != type)
            {
                return false;
            }
            advance();
            return true;
        }

        void advance()
        {
            current = tokenizer.next();
        }

        [[noreturn]] void throwError(const std::string& message) const
        {
            throw std::runtime_error(message + " at position " +
                                     std::to_string(current.position));
        }

        Tokenizer tokenizer;
        Token current;
    };
};

} // namespace NSNAME::graphql
