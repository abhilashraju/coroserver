#pragma once

#include "name_space.hpp"

#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>

#include <cctype>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NSNAME
{

class GraphQLSchema;

struct GraphQLValue
{
    enum class Kind
    {
        Int,
        Float,
        String,
        Boolean,
        Null,
        Enum,
        List,
        Object,
        Variable
    };

    Kind kind = Kind::Null;
    nlohmann::json value = nullptr;
};

struct GraphQLArgument
{
    std::string name;
    GraphQLValue value;
};

struct GraphQLDirective
{
    std::string name;
    std::vector<GraphQLArgument> arguments;
};

struct GraphQLFieldSelection
{
    std::string name;
    std::string alias;
    std::vector<GraphQLArgument> arguments;
    std::vector<GraphQLDirective> directives;
    std::vector<GraphQLFieldSelection> selections;
};

struct GraphQLVariableDefinition
{
    std::string name;
    std::string typeName;
    bool nonNull = false;
    bool isList = false;
    std::optional<GraphQLValue> defaultValue;
};

struct ParsedOperation
{
    enum class OperationType
    {
        Query,
        Mutation,
        Subscription
    };

    OperationType type = OperationType::Query;
    std::string name;
    std::vector<GraphQLVariableDefinition> variableDefinitions;
    std::vector<GraphQLDirective> directives;
    std::vector<GraphQLFieldSelection> selections;
};

class LibGraphQLParser
{
  public:
    static ParsedOperation parse(const std::string& query);
    static bool validate(const std::string& query, std::string& errorMsg);

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

        Token next();

      private:
        void skipIgnored();
        Token readName();
        Token readNumber();
        Token readString();
        bool match(char expected);
        char peek(std::size_t offset = 0) const;
        bool atEnd() const;

        std::string_view input;
        std::size_t position = 0;
    };

    class Parser
    {
      public:
        explicit Parser(std::string_view query);

        ParsedOperation parseDocument();

      private:
        ParsedOperation parseOperationDefinition();
        void parseVariableDefinitions(ParsedOperation& operation);
        std::vector<GraphQLFieldSelection> parseSelectionSet();
        GraphQLFieldSelection parseField();
        std::vector<GraphQLArgument> parseArguments();
        GraphQLDirective parseDirective();
        std::vector<GraphQLDirective> parseDirectives();
        GraphQLValue parseValue();
        GraphQLValue parseListValue();
        GraphQLValue parseObjectValue();
        void parseFragmentDefinition();
        void parseFragmentSpread();
        void parseInlineFragment();
        void skipTypeReference(GraphQLVariableDefinition& definition);
        bool isName(const char* text) const;
        Token expect(TokenType type, const char* message);
        bool accept(TokenType type);
        void advance();
        [[noreturn]] void throwError(const std::string& message) const;

        Tokenizer tokenizer;
        Token current;
    };
};

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

inline bool LibGraphQLParser::Tokenizer::atEnd() const
{
    return position >= input.size();
}

inline char LibGraphQLParser::Tokenizer::peek(std::size_t offset) const
{
    std::size_t index = position + offset;
    if (index >= input.size())
    {
        return '\0';
    }
    return input[index];
}

inline bool LibGraphQLParser::Tokenizer::match(char expected)
{
    if (peek() != expected)
    {
        return false;
    }
    ++position;
    return true;
}

inline void LibGraphQLParser::Tokenizer::skipIgnored()
{
    while (!atEnd())
    {
        char ch = peek();
        if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == ',')
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

inline LibGraphQLParser::Token LibGraphQLParser::Tokenizer::readName()
{
    std::size_t start = position;
    ++position;
    while (!atEnd())
    {
        char ch = peek();
        if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '_')
        {
            break;
        }
        ++position;
    }
    return {TokenType::Name, std::string(input.substr(start, position - start)),
            start};
}

inline LibGraphQLParser::Token LibGraphQLParser::Tokenizer::readNumber()
{
    std::size_t start = position;
    match('-');
    while (std::isdigit(static_cast<unsigned char>(peek())) != 0)
    {
        ++position;
    }

    TokenType type = TokenType::Int;
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1))) != 0)
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

    return {type, std::string(input.substr(start, position - start)), start};
}

inline LibGraphQLParser::Token LibGraphQLParser::Tokenizer::readString()
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
                throw std::runtime_error("Unterminated escape sequence at position " +
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

} // namespace NSNAME
