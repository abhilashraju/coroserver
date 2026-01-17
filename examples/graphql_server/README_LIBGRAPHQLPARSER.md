# GraphQL Server with libgraphqlparser

This is an enhanced version of the GraphQL server that uses **libgraphqlparser** for full-fledged GraphQL query parsing and validation.

## Overview

The libgraphqlparser implementation provides:

- **Full GraphQL Specification Compliance**: Uses Facebook's official libgraphqlparser library
- **Proper AST Parsing**: Complete Abstract Syntax Tree parsing of GraphQL queries
- **Query Validation**: Validates GraphQL queries against the specification
- **Argument Extraction**: Properly extracts and parses query arguments
- **Variable Support**: Full support for GraphQL variables
- **Nested Queries**: Support for nested selection sets
- **Error Reporting**: Detailed error messages for invalid queries

## Architecture

### Components

1. **graphql_parser_libgraphql.hpp**: Header file containing:
   - `ParsedOperation`: Structure representing a parsed GraphQL operation
   - `GraphQLASTVisitor`: AST visitor for extracting operation information
   - `LibGraphQLParser`: Parser using libgraphqlparser
   - `LibGraphQLExecutor`: Executor for parsed operations

2. **graphql_parser_libgraphql.cpp**: Implementation of the executor methods

3. **graphql_server_libgraphql.cpp**: Main server implementation using libgraphqlparser

### Key Classes

#### ParsedOperation
Represents a parsed GraphQL operation with:
- Operation type (Query, Mutation, Subscription)
- Operation name
- Selection set (fields to retrieve)
- Variables
- Field arguments

#### GraphQLASTVisitor
Traverses the GraphQL AST to extract:
- Operation details
- Field selections
- Arguments
- Variables
- Nested selection sets

#### LibGraphQLParser
Static parser class that:
- Parses GraphQL query strings
- Validates query syntax
- Returns ParsedOperation objects
- Provides detailed error messages

#### LibGraphQLExecutor
Executes parsed operations by:
- Resolving queries and mutations
- Merging variables with field arguments
- Calling appropriate resolvers
- Handling errors

## Building

### Prerequisites

Install libgraphqlparser:

```bash
# Ubuntu/Debian
sudo apt-get install libgraphqlparser-dev

# From source
git clone https://github.com/graphql/libgraphqlparser.git
cd libgraphqlparser
cmake -DCMAKE_BUILD_TYPE=Release .
make
sudo make install
```

### Build with Meson

```bash
cd public/sources/coroserver
meson setup build
meson compile -C build
```

The build system will automatically detect libgraphqlparser and build `graphql_server_libgraphql` if available.

### Build with CMake

```bash
cd public/sources/coroserver/examples/graphql_server
mkdir build && cd build
cmake ..
make
```

## Running

```bash
# Run with default settings (port 8443)
./graphql_server_libgraphql

# Run with custom port
./graphql_server_libgraphql --port 9443

# Run with custom certificate directory
./graphql_server_libgraphql --cert /path/to/certs
```

## Differences from Simple Parser

| Feature | Simple Parser | libgraphqlparser |
|---------|--------------|------------------|
| Parsing | Basic string parsing | Full AST parsing |
| Validation | Minimal | Spec-compliant |
| Arguments | Simple extraction | Proper type handling |
| Variables | Basic support | Full variable support |
| Nested Queries | Limited | Full support |
| Error Messages | Generic | Detailed with location |
| Performance | Fast for simple queries | Optimized for complex queries |
| Spec Compliance | Partial | Full |

## Example Queries

### Simple Query
```graphql
query {
  users {
    id
    name
    email
  }
}
```

### Query with Arguments
```graphql
query {
  user(id: 1) {
    id
    name
    email
    age
  }
}
```

### Query with Variables
```graphql
query GetUser($userId: Int!) {
  user(id: $userId) {
    id
    name
    email
  }
}
```

Variables:
```json
{
  "userId": 1
}
```

### Mutation with Arguments
```graphql
mutation {
  createUser(name: "John Doe", email: "john@example.com", age: 30) {
    id
    name
    email
    age
  }
}
```

### Complex Nested Query
```graphql
query {
  users {
    id
    name
    posts {
      id
      title
      content
    }
  }
}
```

## Testing with curl

### Query with Variables
```bash
curl -k -X POST https://localhost:8443/graphql \
  -H "Content-Type: application/json" \
  -d '{
    "query": "query GetUser($userId: Int!) { user(id: $userId) { id name email } }",
    "variables": {
      "userId": 1
    }
  }'
```

### Mutation
```bash
curl -k -X POST https://localhost:8443/graphql \
  -H "Content-Type: application/json" \
  -d '{
    "query": "mutation { createUser(name: \"Jane Doe\", email: \"jane@example.com\", age: 28) { id name email age } }"
  }'
```

### Invalid Query (to see error handling)
```bash
curl -k -X POST https://localhost:8443/graphql \
  -H "Content-Type: application/json" \
  -d '{
    "query": "query { invalid syntax here }"
  }'
```

## Error Handling

The libgraphqlparser implementation provides detailed error messages:

### Parse Error Example
```json
{
  "errors": [
    {
      "message": "GraphQL parse error: syntax error, unexpected IDENTIFIER"
    }
  ]
}
```

### Validation Error Example
```json
{
  "errors": [
    {
      "message": "Unknown query field: invalidField"
    }
  ]
}
```

## Performance Considerations

- **Parsing Overhead**: libgraphqlparser adds minimal overhead for simple queries
- **Complex Queries**: Significantly better performance for complex nested queries
- **Validation**: Upfront validation prevents execution of invalid queries
- **Memory**: Efficient AST representation with automatic cleanup

## Extending the Parser

### Adding Custom Validation

You can extend the `GraphQLASTVisitor` class to add custom validation:

```cpp
class CustomValidator : public GraphQLASTVisitor {
public:
    void validateCustomRules(const ParsedOperation& op) {
        // Add your custom validation logic
    }
};
```

### Supporting Additional GraphQL Features

The implementation can be extended to support:
- Fragments
- Directives
- Subscriptions (real-time updates)
- Custom scalars
- Input types

## Troubleshooting

### libgraphqlparser not found

If the build system cannot find libgraphqlparser:

1. Check installation:
   ```bash
   pkg-config --exists libgraphqlparser && echo "Found" || echo "Not found"
   ```

2. Set PKG_CONFIG_PATH:
   ```bash
   export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
   ```

3. Verify library location:
   ```bash
   ldconfig -p | grep graphqlparser
   ```

### Runtime Errors

If you get runtime errors:

1. Check library path:
   ```bash
   export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
   ```

2. Verify the library is loaded:
   ```bash
   ldd ./graphql_server_libgraphql | grep graphqlparser
   ```

## Resources

- [libgraphqlparser GitHub](https://github.com/graphql/libgraphqlparser)
- [GraphQL Specification](https://spec.graphql.org/)
- [GraphQL Official Site](https://graphql.org/)
- [GraphQL Best Practices](https://graphql.org/learn/best-practices/)

## Comparison with Other Implementations

### vs Simple Parser (graphql_server.cpp)
- ✅ Full spec compliance
- ✅ Better error messages
- ✅ Proper validation
- ⚠️ Slightly more overhead

### vs cppgraphqlgen
- ✅ Runtime parsing (no code generation)
- ✅ More flexible
- ✅ Easier to extend
- ⚠️ Less type safety

## Future Enhancements

Planned features:
- [ ] Fragment support
- [ ] Directive support
- [ ] Subscription support
- [ ] Schema introspection via libgraphqlparser
- [ ] Query complexity analysis
- [ ] Rate limiting based on query complexity
- [ ] Caching of parsed queries

## License

Same as the coroserver project.

## Contributing

Contributions are welcome! Please ensure:
- Code follows the existing style
- All tests pass
- Documentation is updated
- Examples are provided for new features

---

Made with Bob