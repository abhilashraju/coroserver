# GraphQL Server Example

A coroutine-based GraphQL server implementation using the coroserver APIs.

## Available Implementations

This example provides **two implementations**:

1. **graphql_server** - Simple built-in parser (no external dependencies)
2. **graphql_server_libgraphql** - Full-fledged parser using libgraphqlparser

### Simple Parser (graphql_server)
- Lightweight, no external dependencies
- Basic GraphQL query parsing
- Suitable for simple use cases
- Fast compilation and minimal overhead

### libgraphqlparser Implementation (graphql_server_libgraphql)
- **Full GraphQL specification compliance**
- **Proper AST parsing and validation**
- **Complete argument and variable support**
- **Detailed error messages**
- See [README_LIBGRAPHQLPARSER.md](README_LIBGRAPHQLPARSER.md) for details

## Features

- **Coroutine-based**: Uses C++20 coroutines for async operations
- **HTTP/HTTPS Support**: Built on top of coroserver's HTTP server
- **GraphQL Support**: Query and mutation operations
- **In-memory Data Store**: Sample implementation with Users and Posts
- **SSL/TLS**: Secure connections using OpenSSL
- **Multiple Parser Options**: Choose between simple or full-featured parser

## Building

This example is built as part of the coroserver project using Meson:

```bash
cd public/sources/coroserver
meson setup build
meson compile -C build
```

The executable will be created at: `build/examples/graphql_server/graphql_server`

## Running

```bash
# Run with default settings (port 8443)
./graphql_server

# Run with custom port
./graphql_server --port 9443

# Run with custom certificate directory
./graphql_server --cert /path/to/certs
```

## API Endpoints

### POST /graphql
Main GraphQL endpoint for queries and mutations.

### GET /health
Health check endpoint.

### GET /schema
Returns schema documentation.

## GraphQL Schema

### Types

#### User
```graphql
type User {
  id: Int!
  name: String!
  email: String!
  age: Int!
}
```

#### Post
```graphql
type Post {
  id: Int!
  title: String!
  content: String!
  authorId: Int!
}
```

### Queries

#### users
Get all users.
```graphql
query {
  users
}
```

#### user(id: Int!)
Get a specific user by ID.
```graphql
query {
  user(id: 1)
}
```

#### posts
Get all posts.
```graphql
query {
  posts
}
```

#### postsByAuthor(authorId: Int!)
Get posts by a specific author.
```graphql
query {
  postsByAuthor(authorId: 1)
}
```

### Mutations

#### createUser(name: String!, email: String!, age: Int)
Create a new user.
```graphql
mutation {
  createUser(name: "John Doe", email: "john@example.com", age: 28)
}
```

#### createPost(title: String!, content: String!, authorId: Int!)
Create a new post.
```graphql
mutation {
  createPost(title: "My Post", content: "Post content", authorId: 1)
}
```

#### updateUser(id: Int!, name: String, email: String, age: Int)
Update an existing user.
```graphql
mutation {
  updateUser(id: 1, name: "Alice Smith", age: 31)
}
```

## Example Usage with curl

### Query all users
```bash
curl -k -X POST https://localhost:8443/graphql \
  -H "Content-Type: application/json" \
  -d '{
    "query": "{ users }"
  }'
```

### Query specific user
```bash
curl -k -X POST https://localhost:8443/graphql \
  -H "Content-Type: application/json" \
  -d '{
    "query": "{ user(id: 1) }"
  }'
```

### Query all posts
```bash
curl -k -X POST https://localhost:8443/graphql \
  -H "Content-Type: application/json" \
  -d '{
    "query": "{ posts }"
  }'
```

### Query posts by author
```bash
curl -k -X POST https://localhost:8443/graphql \
  -H "Content-Type: application/json" \
  -d '{
    "query": "{ postsByAuthor(authorId: 1) }"
  }'
```

### Create a new user
```bash
curl -k -X POST https://localhost:8443/graphql \
  -H "Content-Type: application/json" \
  -d '{
    "query": "mutation { createUser(name: \"David Lee\", email: \"david@example.com\", age: 27) }"
  }'
```

### Create a new post
```bash
curl -k -X POST https://localhost:8443/graphql \
  -H "Content-Type: application/json" \
  -d '{
    "query": "mutation { createPost(title: \"New Post\", content: \"This is a new post\", authorId: 1) }"
  }'
```

### Update a user
```bash
curl -k -X POST https://localhost:8443/graphql \
  -H "Content-Type: application/json" \
  -d '{
    "query": "mutation { updateUser(id: 1, name: \"Alice Johnson Updated\", age: 32) }"
  }'
```

### Health check
```bash
curl -k https://localhost:8443/health
```

### Get schema documentation
```bash
curl -k https://localhost:8443/schema
```

## Example Responses

### Successful Query Response
```json
{
  "data": {
    "users": [
      {
        "id": 1,
        "name": "Alice Johnson",
        "email": "alice@example.com",
        "age": 30
      },
      {
        "id": 2,
        "name": "Bob Smith",
        "email": "bob@example.com",
        "age": 25
      }
    ]
  }
}
```

### Error Response
```json
{
  "errors": [
    {
      "message": "User not found"
    }
  ]
}
```

## Architecture

### Components

1. **graphql_handler.hpp**: Contains the GraphQL schema, parser, and executor
   - `GraphQLSchema`: Manages types, queries, and mutations
   - `GraphQLParser`: Simple GraphQL query parser
   - `GraphQLExecutor`: Executes GraphQL queries and mutations

2. **graphql_server.cpp**: Main server implementation
   - HTTP server setup using coroserver APIs
   - Data store implementation (Users and Posts)
   - GraphQL resolvers for queries and mutations
   - Coroutine-based request handling

### Coroutine Usage

The server uses C++20 coroutines through Boost.Asio's `awaitable` type:

```cpp
router.add_post_handler(
    "/graphql",
    [executor](Request& req, const http_function& params) -> net::awaitable<Response> {
        // Async operations using co_await
        nlohmann::json response = executor->execute(query, variables);
        co_return make_success_response(response, http::status::ok, req.version());
    });
```

## Extending the Server

### Adding New Types

1. Define the type structure in the data store
2. Add resolver functions in `setupSchema()`
3. Register queries/mutations with the schema

### Adding New Queries

```cpp
schema->addQuery("myQuery", [store](const nlohmann::json& args, const nlohmann::json& context) {
    // Your query logic here
    return nlohmann::json{{"result", "value"}};
});
```

### Adding New Mutations

```cpp
schema->addMutation("myMutation", [store](const nlohmann::json& args, const nlohmann::json& context) {
    // Your mutation logic here
    return nlohmann::json{{"success", true}};
});
```

## Notes

- This is a simplified GraphQL implementation for demonstration purposes
- For production use, consider using a full-featured GraphQL library
- The parser is basic and supports simple queries; complex nested queries may require a more robust parser
- SSL certificates are self-signed for development; use proper certificates in production
- The data store is in-memory; data is lost when the server restarts

## Dependencies

- Boost.Asio (for coroutines and networking)
- Boost.Beast (for HTTP)
- OpenSSL (for SSL/TLS)
- nlohmann/json (for JSON parsing)
- coroserver library (for HTTP server infrastructure)

## Resources

- [GraphQL Official Site](https://graphql.org/)
- [GraphQL Specification](https://spec.graphql.org/)
- [GraphQL Best Practices](https://graphql.org/learn/best-practices/)
- [Coroserver Documentation](../../README.md)

## License

Same as the coroserver project.