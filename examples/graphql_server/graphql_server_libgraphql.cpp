#include "command_line_parser.hpp"
#include "graphql_handler.hpp"
#include "graphql_parser_libgraphql.hpp"
#include "http_server.hpp"
#include "logger.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace NSNAME;

// Sample data store
struct User
{
    int id;
    std::string name;
    std::string email;
    int age;
};

struct Post
{
    int id;
    std::string title;
    std::string content;
    int authorId;
};

// In-memory data store
class DataStore
{
  public:
    DataStore()
    {
        // Initialize with sample data
        users = {{1, "Alice Johnson", "alice@example.com", 30},
                 {2, "Bob Smith", "bob@example.com", 25},
                 {3, "Charlie Brown", "charlie@example.com", 35}};

        posts = {{1, "First Post", "This is my first post!", 1},
                 {2, "GraphQL Tutorial", "Learning GraphQL with C++", 1},
                 {3, "Coroutines are awesome", "Async programming made easy",
                  2}};
    }

    std::vector<User> users;
    std::vector<Post> posts;
    int nextUserId = 4;
    int nextPostId = 4;
};

// Setup GraphQL schema with resolvers
std::shared_ptr<GraphQLSchema> setupSchema(std::shared_ptr<DataStore> store)
{
    auto schema = std::make_shared<GraphQLSchema>();

    // Query: Get all users
    schema->addQuery(
        "users",
        [store](const nlohmann::json& args, const nlohmann::json& context)
            -> boost::asio::awaitable<nlohmann::json> {
            nlohmann::json result = nlohmann::json::array();
            for (const auto& user : store->users)
            {
                result.push_back({{"id", user.id},
                                  {"name", user.name},
                                  {"email", user.email},
                                  {"age", user.age}});
            }
            co_return result;
        });

    // Query: Get user by ID
    schema->addQuery(
        "user",
        [store](const nlohmann::json& args, const nlohmann::json& context)
            -> boost::asio::awaitable<nlohmann::json> {
            if (!args.contains("id"))
            {
                throw std::runtime_error("Missing required argument: id");
            }
            std::cout << "args[\"id\"] type: " << args["id"].type_name()
                      << std::endl;
            std::cout << "args[\"id\"] value: " << args["id"].dump()
                      << std::endl;
            int id = args["id"].is_string()
                         ? std::stoi(args["id"].get<std::string>())
                         : args["id"].get<int>();

            for (const auto& user : store->users)
            {
                if (user.id == id)
                {
                    co_return nlohmann::json{{"id", user.id},
                                             {"name", user.name},
                                             {"email", user.email},
                                             {"age", user.age}};
                }
            }
            co_return nlohmann::json(nullptr);
        });

    // Query: Get all posts
    schema->addQuery(
        "posts",
        [store](const nlohmann::json& args, const nlohmann::json& context)
            -> boost::asio::awaitable<nlohmann::json> {
            nlohmann::json result = nlohmann::json::array();
            for (const auto& post : store->posts)
            {
                result.push_back({{"id", post.id},
                                  {"title", post.title},
                                  {"content", post.content},
                                  {"authorId", post.authorId}});
            }
            co_return result;
        });

    // Query: Get posts by author
    schema->addQuery(
        "postsByAuthor",
        [store](const nlohmann::json& args, const nlohmann::json& context)
            -> boost::asio::awaitable<nlohmann::json> {
            if (!args.contains("authorId"))
            {
                throw std::runtime_error("Missing required argument: authorId");
            }

            int authorId = args["authorId"].is_string()
                               ? std::stoi(args["authorId"].get<std::string>())
                               : args["authorId"].get<int>();

            nlohmann::json result = nlohmann::json::array();
            for (const auto& post : store->posts)
            {
                if (post.authorId == authorId)
                {
                    result.push_back({{"id", post.id},
                                      {"title", post.title},
                                      {"content", post.content},
                                      {"authorId", post.authorId}});
                }
            }
            co_return result;
        });

    // Mutation: Create user
    schema->addMutation(
        "createUser",
        [store](const nlohmann::json& args, const nlohmann::json& context)
            -> boost::asio::awaitable<nlohmann::json> {
            if (!args.contains("name") || !args.contains("email"))
            {
                throw std::runtime_error(
                    "Missing required arguments: name, email");
            }

            User newUser;
            newUser.id = store->nextUserId++;
            newUser.name = args["name"].get<std::string>();
            newUser.email = args["email"].get<std::string>();
            newUser.age = args.contains("age")
                              ? (args["age"].is_string()
                                     ? std::stoi(args["age"].get<std::string>())
                                     : args["age"].get<int>())
                              : 0;

            store->users.push_back(newUser);

            co_return nlohmann::json{{"id", newUser.id},
                                     {"name", newUser.name},
                                     {"email", newUser.email},
                                     {"age", newUser.age}};
        });

    // Mutation: Create post
    schema->addMutation(
        "createPost",
        [store](const nlohmann::json& args, const nlohmann::json& context)
            -> boost::asio::awaitable<nlohmann::json> {
            if (!args.contains("title") || !args.contains("content") ||
                !args.contains("authorId"))
            {
                throw std::runtime_error(
                    "Missing required arguments: title, content, authorId");
            }

            Post newPost;
            newPost.id = store->nextPostId++;
            newPost.title = args["title"].get<std::string>();
            newPost.content = args["content"].get<std::string>();
            newPost.authorId =
                args["authorId"].is_string()
                    ? std::stoi(args["authorId"].get<std::string>())
                    : args["authorId"].get<int>();

            store->posts.push_back(newPost);

            co_return nlohmann::json{{"id", newPost.id},
                                     {"title", newPost.title},
                                     {"content", newPost.content},
                                     {"authorId", newPost.authorId}};
        });

    // Mutation: Update user
    schema->addMutation(
        "updateUser",
        [store](const nlohmann::json& args, const nlohmann::json& context)
            -> boost::asio::awaitable<nlohmann::json> {
            if (!args.contains("id"))
            {
                throw std::runtime_error("Missing required argument: id");
            }

            int id = args["id"].is_string()
                         ? std::stoi(args["id"].get<std::string>())
                         : args["id"].get<int>();

            for (auto& user : store->users)
            {
                if (user.id == id)
                {
                    if (args.contains("name"))
                    {
                        user.name = args["name"].get<std::string>();
                    }
                    if (args.contains("email"))
                    {
                        user.email = args["email"].get<std::string>();
                    }
                    if (args.contains("age"))
                    {
                        user.age =
                            args["age"].is_string()
                                ? std::stoi(args["age"].get<std::string>())
                                : args["age"].get<int>();
                    }

                    co_return nlohmann::json{{"id", user.id},
                                             {"name", user.name},
                                             {"email", user.email},
                                             {"age", user.age}};
                }
            }

            throw std::runtime_error("User not found");
        });

    return schema;
}

int main(int argc, const char* argv[])
{
    try
    {
        auto [cert, port] =
            getArgs(parseCommandline(argc, argv), "--cert,-c", "--port,-p");

        boost::asio::io_context io_context;

        // Initialize data store and schema
        auto dataStore = std::make_shared<DataStore>();
        auto schema = setupSchema(dataStore);
        auto executor = std::make_shared<LibGraphQLExecutor>(schema);

        // Setup SSL context
        boost::asio::ssl::context ssl_context(
            boost::asio::ssl::context::sslv23);
        ssl_context.set_options(boost::asio::ssl::context::default_workarounds |
                                boost::asio::ssl::context::no_sslv2 |
                                boost::asio::ssl::context::single_dh_use);

        std::string certDir = cert ? std::string(*cert) : ".";
        ssl_context.use_certificate_chain_file(certDir + "/server-cert.pem");
        ssl_context.use_private_key_file(certDir + "/server-key.pem",
                                         boost::asio::ssl::context::pem);

        // Setup HTTP router
        HttpRouter router;
        router.setIoContext(io_context);

        // GraphQL endpoint - POST /graphql
        router.add_post_handler(
            "/graphql",
            [executor](Request& req, const http_function& params)
                -> net::awaitable<Response> {
                nlohmann::json requestBody;

                try
                {
                    requestBody =
                        nlohmann::json::parse(req.body(), nullptr, false);
                }
                catch (const nlohmann::json::parse_error& e)
                {
                    co_return make_bad_request_error(
                        "Invalid JSON in request body", req.version());
                }

                if (requestBody.is_discarded() ||
                    !requestBody.contains("query"))
                {
                    co_return make_bad_request_error(
                        "Missing 'query' field in request", req.version());
                }

                std::string query = requestBody["query"].get<std::string>();
                nlohmann::json variables = requestBody.contains("variables")
                                               ? requestBody["variables"]
                                               : nlohmann::json::object();

                // Execute GraphQL query using libgraphqlparser
                nlohmann::json response =
                    co_await executor->execute(query, variables);

                co_return make_success_response(response, http::status::ok,
                                                req.version());
            });

        // Health check endpoint
        router.add_get_handler(
            "/health",
            [](Request& req, const http_function& params) -> Response {
                nlohmann::json response = {
                    {"status", "healthy"},
                    {"service", "GraphQL Server (libgraphqlparser)"}};
                return make_success_response(response, http::status::ok,
                                             req.version());
            });

        // Schema introspection endpoint
        router.add_get_handler(
            "/schema",
            [](Request& req, const http_function& params) -> Response {
                nlohmann::json schemaDoc = {
                    {"queries",
                     {{"users", "Get all users"},
                      {"user", "Get user by id (args: id)"},
                      {"posts", "Get all posts"},
                      {"postsByAuthor",
                       "Get posts by author (args: authorId)"}}},
                    {"mutations",
                     {{"createUser",
                       "Create a new user (args: name, email, age)"},
                      {"createPost",
                       "Create a new post (args: title, content, authorId)"},
                      {"updateUser",
                       "Update user (args: id, name?, email?, age?)"}}}};
                return make_success_response(schemaDoc, http::status::ok,
                                             req.version());
            });

        // Setup server
        int serverPort = port ? std::stoi(std::string(*port)) : 8443;
        TcpStreamType acceptor(io_context.get_executor(), serverPort,
                               ssl_context);
        HttpServer server(io_context, acceptor, router);

        LOG_INFO("GraphQL Server (libgraphqlparser) started on port {}",
                 serverPort);
        LOG_INFO("Endpoints:");
        LOG_INFO("  POST /graphql - GraphQL queries and mutations");
        LOG_INFO("  GET  /health  - Health check");
        LOG_INFO("  GET  /schema  - Schema documentation");
        LOG_INFO("Using libgraphqlparser for full GraphQL parsing");

        io_context.run();
    }
    catch (std::exception& e)
    {
        LOG_ERROR("Exception: {}", e.what());
        return 1;
    }

    return 0;
}

// Made with Bob
