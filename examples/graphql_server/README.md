# GraphQL Server — coroserver

A zero-external-dependency, coroutine-based GraphQL server framework built on top of the coroserver HTTP/HTTPS stack. The active example ships a fully functional **Redfish GraphQL Server** and the framework is designed to be extended for any domain-specific schema.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Component Diagram](#component-diagram)
3. [Request Lifecycle](#request-lifecycle)
4. [Core Abstractions](#core-abstractions)
5. [Building & Running](#building--running)
6. [Redfish Example — Queries](#redfish-example--queries)
7. [Creating a Domain-Specific GraphQL Server](#creating-a-domain-specific-graphql-server)
   - [Step 1 — Define Object Types in the Schema](#step-1--define-object-types-in-the-schema)
   - [Step 2 — Register Root Queries](#step-2--register-root-queries)
   - [Step 3 — Implement the Data Provider](#step-3--implement-the-data-provider)
   - [Step 4 — Implement the Executor](#step-4--implement-the-executor)
   - [Step 5 — Wire the Server Entry Point](#step-5--wire-the-server-entry-point)
8. [Full Walk-through: Inventory GraphQL Server](#full-walk-through-inventory-graphql-server)
9. [API Endpoints](#api-endpoints)
10. [File Reference](#file-reference)
11. [Dependencies](#dependencies)

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                          Client (curl / browser)                    │
│           POST /graphql  { "query": "{ systems { id name } }" }     │
└──────────────────────────────────┬──────────────────────────────────┘
                                   │  HTTPS / TLS
┌──────────────────────────────────▼──────────────────────────────────┐
│                        HttpServer  (coroserver)                     │
│   Boost.Asio io_context · Boost.Beast HTTP · OpenSSL SSL context   │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                        HttpRouter                            │  │
│  │   POST /graphql ──► handler coroutine                        │  │
│  │   GET  /health  ──► inline handler                           │  │
│  │   GET  /schema  ──► inline handler                           │  │
│  └──────────────────────────────┬───────────────────────────────┘  │
└─────────────────────────────────┼───────────────────────────────────┘
                                  │  co_await executor->execute(query)
┌─────────────────────────────────▼───────────────────────────────────┐
│                        TypedExecutor<Provider>                      │
│  ┌──────────────┐   ┌─────────────────┐   ┌──────────────────────┐ │
│  │   Parser     │   │   TypedSchema   │   │  resolveRootField()  │ │
│  │  (in-tree,   │──►│  validateOp()   │──►│  (override per       │ │
│  │  no deps)    │   │  getRootQuery() │   │   domain)            │ │
│  │  parse()     │   │  getObject()    │   │                      │ │
│  │  validate()  │   └─────────────────┘   └──────────┬───────────┘ │
│  └──────────────┘                                    │             │
│           AST: Operation / FieldSelection / Argument │             │
└─────────────────────────────────────────────────────┼─────────────┘
                                                       │  co_await provider->get(url)
┌──────────────────────────────────────────────────────▼─────────────┐
│                       Provider (abstract)                           │
│   virtual awaitable<json> get(const string& target) = 0            │
│                                                                     │
│   HttpRedfishProvider   ·   MockProvider   ·   YourProvider        │
│   (real HTTPS client)       (unit tests)       (your backend)      │
└────────────────────────────────────────────────────────────────────┘
```

---

## Component Diagram

```
include/graphql/
├── ast.hpp              ← Value, Argument, FieldSelection, Operation  (plain data)
├── parser.hpp           ← Parser::parse() / Parser::validate()
├── typed_schema.hpp     ← ArgumentSpec, FieldSpec, ObjectSpec, TypedSchema
├── typed_executor.hpp   ← TypedExecutor<Provider>  (template base class)
├── simple_schema.hpp    ← lightweight non-typed variant
├── simple_executor.hpp  ← lightweight non-typed variant
└── util.hpp             ← argumentsToJson(), helpers

Domain-specific files (Redfish example):
├── graphql_redfish_schema.hpp/.cpp    ← builds TypedSchema
├── graphql_redfish_provider.hpp/.cpp  ← RedfishProvider (abstract) + HttpRedfishProvider
├── graphql_redfish_executor.hpp/.cpp  ← overrides resolveRootField()
└── graphql_redfish_server.cpp         ← main(): HttpServer + HttpRouter wiring
```

---

## Request Lifecycle

```
Client sends:
  POST /graphql
  { "query": "{ systems { id name powerState } }", "variables": {} }

  Step 1 ── HTTP layer
  HttpServer accepts TLS connection → HttpRouter dispatches to /graphql handler

  Step 2 ── JSON parse
  nlohmann::json::parse(req.body())  → extract "query" and "variables"

  Step 3 ── GraphQL parse
  Parser::validate(query)            → lexical + syntax check (throws on error)
  Parser::parse(query)               → produces Operation (AST root)

  Step 4 ── Schema validation
  TypedSchema::validateOperation()   → checks field names, argument presence/types

  Step 5 ── Execution  (coroutine)
  TypedExecutor::executeSelections() → for each root FieldSelection:
    └── resolveRootField()           → domain-specific override:
          maps field name → backend URL / data source
          co_await provider->get(url) → raw JSON payload

  Step 6 ── Projection
  TypedExecutor::projectObject()     → filters raw JSON to only requested fields
  TypedExecutor::projectField()      → recurses into nested objects / lists

  Step 7 ── Response
  { "data": { "systems": [ { "id": "1", "name": "sys", "powerState": "On" } ] } }
  HTTP 200 returned to client
```

---

## Core Abstractions

### `TypedSchema`  ([`include/graphql/typed_schema.hpp`](include/graphql/typed_schema.hpp))

Holds the full type registry and root query map.

| API | Purpose |
|-----|---------|
| `addObject(ObjectSpec)` | Register a named object type with its fields |
| `addRootQuery(FieldSpec)` | Register a top-level query entry point |
| `getRootQueryField(name)` | Look up a root query by name |
| `getObject(typeName)` | Look up an object type by name |
| `validateOperation(op)` | Validate an AST operation against the schema |

**`FieldSpec`** — describes one field:

```cpp
struct FieldSpec {
    std::string name;        // GraphQL field name  e.g. "powerState"
    std::string responseKey; // key in backend JSON e.g. "PowerState"
    std::string returnType;  // type name           e.g. "String" / "Status"
    bool isList   = false;   // returns an array?
    bool scalar   = false;   // primitive (no sub-selections)?
    std::vector<ArgumentSpec> arguments;
};
```

---

### `TypedExecutor<Provider>` ([`include/graphql/typed_executor.hpp`](include/graphql/typed_executor.hpp))

Template base class — parameterised on the data provider type.

```
             TypedExecutor<Provider>
             ┌───────────────────────────────────────────┐
             │  execute(query, variables)                │  ← public entry
             │    Parser::parse()                        │
             │    schema.validateOperation()             │
             │    executeSelections()                    │
             │      resolveRootField()  ◄── pure virtual │
             │      projectObject()                      │
             │      projectField()                       │
             └───────────────────────────────────────────┘
                              ▲
                              │ override resolveRootField()
              YourGraphQLExecutor : TypedExecutor<YourProvider>
```

The only method you **must** implement is:

```cpp
boost::asio::awaitable<nlohmann::json> resolveRootField(
    const graphql::FieldSelection& selection,
    const graphql::FieldSpec& fieldSpec,
    const nlohmann::json& variables) override;
```

---

### `Parser`  ([`include/graphql/parser.hpp`](include/graphql/parser.hpp))

Zero-dependency handwritten recursive-descent lexer + parser.  Produces an `Operation` AST.

```
Parser::parse(queryString)
  └── Impl::parseDocument()
        ├── parseOperation()      (query / mutation / subscription)
        │     ├── parseVariableDefinitions()
        │     └── parseSelectionSet()
        │           └── parseField()
        │                 ├── parseArguments()
        │                 └── parseSelectionSet()  (recursive)
        └── returns Operation { type, name, variableDefinitions, selections }
```

---

## Building & Running

```bash
# From the coroserver root
meson setup build
meson compile -C build

# The executable
build/examples/graphql_server/graphql_redfish_server
```

### Command-line options

| Flag | Short | Default | Description |
|------|-------|---------|-------------|
| `--port` | `-p` | `8444` | Listening port |
| `--cert` | `-c` | `/etc/ssl/certs/https` | Directory containing `server-cert.pem` / `server-key.pem` |
| `--host` | `-h` | `localhost` | Redfish target host |
| `--target-port` | `-t` | `443` | Redfish target port |
| `--user` | `-u` | `root` | Redfish username |
| `--password` | `-w` | `0penBmc` | Redfish password |

```bash
./graphql_redfish_server \
  --port 8444 \
  --cert ./examples/graphql_server \
  --host 192.168.1.10 \
  --target-port 443 \
  --user admin \
  --password secret
```

---

## Redfish Example — Queries

### Get service root

```bash
curl -k -X POST https://localhost:8444/graphql \
  -H 'Content-Type: application/json' \
  -d '{"query": "{ serviceRoot { id name } }"}'
```

### List all systems

```bash
curl -k -X POST https://localhost:8444/graphql \
  -H 'Content-Type: application/json' \
  -d '{"query": "{ systems { id name powerState status { health state } } }"}'
```

### Get a single system by ID

```bash
curl -k -X POST https://localhost:8444/graphql \
  -H 'Content-Type: application/json' \
  -d '{"query": "{ system(id: \"1\") { id name powerState processorSummary { count model } } }"}'
```

### Query with variables

```bash
curl -k -X POST https://localhost:8444/graphql \
  -H 'Content-Type: application/json' \
  -d '{
    "query": "query GetSystem($id: ID!) { system(id: $id) { id name powerState } }",
    "variables": { "id": "1" }
  }'
```

### Ethernet interfaces with nested IP addresses

```bash
curl -k -X POST https://localhost:8444/graphql \
  -H 'Content-Type: application/json' \
  -d '{
    "query": "{ ethernetInterfaces { id name macAddress linkStatus ipv4Addresses { address subnetMask gateway } } }"
  }'
```

### Field aliases

```bash
curl -k -X POST https://localhost:8444/graphql \
  -H 'Content-Type: application/json' \
  -d '{
    "query": "{ bmc: managers { id name status { health } } allChassis: chassis { id name } }"
  }'
```

---

## Creating a Domain-Specific GraphQL Server

The framework requires five artefacts for a new domain.  The Redfish implementation is the reference; the steps below use an **Inventory** domain (servers, racks, and alerts) as a fresh example.

### Step 1 — Define Object Types in the Schema

Create `inventory_schema.cpp`.  Each `ObjectSpec` maps **GraphQL field names** to **backend JSON keys** via `FieldSpec`.

```cpp
// inventory_schema.cpp
#include "graphql/typed_schema.hpp"

graphql::TypedSchema buildInventorySchema()
{
    graphql::TypedSchema schema;

    // ── Scalar-only leaf types ──────────────────────────────────────
    graphql::ObjectSpec alertSpec{
        "Alert",
        {
            // {graphqlName, backendJsonKey, returnType, isList, isScalar}
            {"id",       {"id",       "alertId",   "String", false, true}},
            {"severity", {"severity", "severity",  "String", false, true}},
            {"message",  {"message",  "message",   "String", false, true}},
        }
    };

    // ── Nested object type ──────────────────────────────────────────
    graphql::ObjectSpec serverSpec{
        "Server",
        {
            {"id",       {"id",       "server_id", "String", false, true}},
            {"hostname", {"hostname", "hostname",  "String", false, true}},
            {"rack",     {"rack",     "rack_name", "String", false, true}},
            // isList=true → maps to a JSON array of Alert objects
            {"alerts",   {"alerts",   "alerts",    "Alert",  true,  false}},
        }
    };

    graphql::ObjectSpec rackSpec{
        "Rack",
        {
            {"id",      {"id",      "rack_id",  "String", false, true}},
            {"name",    {"name",    "name",     "String", false, true}},
            // isList=true, isScalar=false → array of nested Server objects
            {"servers", {"servers", "servers",  "Server", true,  false}},
        }
    };

    schema.addObject(std::move(alertSpec));
    schema.addObject(std::move(serverSpec));
    schema.addObject(std::move(rackSpec));

    return schema;
}
```

**`FieldSpec` field meanings at a glance:**

```
{"graphqlName", "backendKey", "ReturnType", isList, isScalar}
                                              │        │
                                              │        └── true  → primitive (String/Int/Boolean)
                                              │            false → resolve sub-selections against ReturnType
                                              └────────── true  → JSON array
                                                          false → single object
```

---

### Step 2 — Register Root Queries

Root queries are the entry points a client can use at the top level of `{ ... }`.

```cpp
// still in inventory_schema.cpp, after addObject() calls

// No arguments — returns a list of Rack objects
schema.addRootQuery({"racks",   "", "Rack",   true,  false});

// No arguments — returns a list of Server objects
schema.addRootQuery({"servers", "", "Server", true,  false});

// Required "id" argument — returns a single Server
schema.addRootQuery({
    "server", "", "Server", false, false,
    {{"id", "String", /*required=*/true}}
});

// Optional severity filter — returns a list of Alerts
schema.addRootQuery({
    "alerts", "", "Alert", true, false,
    {{"severity", "String", /*required=*/false}}
});

return schema;
```

---

### Step 3 — Implement the Data Provider

The provider interface has a single coroutine method.  Implement it to fetch data from your backend (REST API, database, D-Bus, etc.).

```cpp
// inventory_provider.hpp
#pragma once
#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>
#include <string>

class InventoryProvider {
public:
    virtual ~InventoryProvider() = default;
    virtual boost::asio::awaitable<nlohmann::json>
        get(const std::string& resource) = 0;
};

// ── Concrete implementation ─────────────────────────────────────────
class HttpInventoryProvider : public InventoryProvider {
public:
    HttpInventoryProvider(boost::asio::io_context& io,
                          const std::string& baseUrl);

    boost::asio::awaitable<nlohmann::json>
        get(const std::string& resource) override;

private:
    boost::asio::io_context& io_;
    std::string baseUrl_;
    // ... HTTP client members
};

// ── In-process mock (unit tests / development) ──────────────────────
class MockInventoryProvider : public InventoryProvider {
public:
    boost::asio::awaitable<nlohmann::json>
        get(const std::string& resource) override
    {
        if (resource == "/inventory/racks")
        {
            co_return nlohmann::json::array({
                {{"rack_id","R1"},{"name","Rack-A"},{"servers",nlohmann::json::array()}},
                {{"rack_id","R2"},{"name","Rack-B"},{"servers",nlohmann::json::array()}}
            });
        }
        if (resource == "/inventory/servers")
        {
            co_return nlohmann::json::array({
                {{"server_id","S1"},{"hostname","web-01"},{"rack_name","Rack-A"},{"alerts",nlohmann::json::array()}},
                {{"server_id","S2"},{"hostname","db-01"}, {"rack_name","Rack-B"},{"alerts",nlohmann::json::array()}}
            });
        }
        co_return nlohmann::json::object();
    }
};
```

---

### Step 4 — Implement the Executor

Override `resolveRootField()` to map each root query name to a backend fetch.

```cpp
// inventory_executor.hpp
#pragma once
#include "graphql/typed_executor.hpp"
#include "inventory_provider.hpp"

class InventoryGraphQLExecutor
    : public graphql::TypedExecutor<InventoryProvider>
{
public:
    InventoryGraphQLExecutor(graphql::TypedSchema schema,
                             std::shared_ptr<InventoryProvider> provider)
        : graphql::TypedExecutor<InventoryProvider>(
              std::move(schema), std::move(provider))
    {}

protected:
    boost::asio::awaitable<nlohmann::json> resolveRootField(
        const graphql::FieldSelection& selection,
        const graphql::FieldSpec& fieldSpec,
        const nlohmann::json& variables) override;
};

// inventory_executor.cpp
boost::asio::awaitable<nlohmann::json>
InventoryGraphQLExecutor::resolveRootField(
    const graphql::FieldSelection& selection,
    const graphql::FieldSpec& fieldSpec,
    const nlohmann::json& variables)
{
    nlohmann::json args = resolveArguments(selection, variables);
    std::string resource;

    if (selection.name == "racks")
        resource = "/inventory/racks";
    else if (selection.name == "servers")
        resource = "/inventory/servers";
    else if (selection.name == "server")
        resource = "/inventory/servers/" + args["id"].get<std::string>();
    else if (selection.name == "alerts")
        resource = "/inventory/alerts";
    else
        throw std::runtime_error("Unknown field: " + selection.name);

    nlohmann::json payload = co_await provider->get(resource);

    // ── List result: each element is projected through its ObjectSpec ──
    if (fieldSpec.isList)
    {
        if (!payload.is_array())
            throw std::runtime_error("Expected array for " + selection.name);

        nlohmann::json result = nlohmann::json::array();
        for (const auto& item : payload)
            result.push_back(
                co_await projectObject(item, fieldSpec.returnType,
                                       selection.selections));
        co_return result;
    }

    // ── Single object result ──────────────────────────────────────────
    co_return co_await projectObject(payload, fieldSpec.returnType,
                                     selection.selections);
}
```

**`resolveArguments()`** (inherited from `TypedExecutor`) converts the `FieldSelection`'s argument list to a `nlohmann::json` object, substituting `$variable` references from the `variables` map automatically.

---

### Step 5 — Wire the Server Entry Point

```cpp
// inventory_server.cpp
#include "graphql/typed_schema.hpp"
#include "http_server.hpp"
#include "inventory_executor.hpp"
#include "inventory_provider.hpp"

int main(int argc, const char* argv[])
{
    boost::asio::io_context io;

    // 1. Schema
    auto schema = buildInventorySchema();

    // 2. Provider  (swap MockInventoryProvider for unit tests)
    auto provider = std::make_shared<MockInventoryProvider>();

    // 3. Executor
    auto executor = std::make_shared<InventoryGraphQLExecutor>(
        std::move(schema), provider);

    // 4. SSL context
    boost::asio::ssl::context ssl(boost::asio::ssl::context::sslv23);
    ssl.use_certificate_chain_file("server-cert.pem");
    ssl.use_private_key_file("server-key.pem", boost::asio::ssl::context::pem);

    // 5. Router
    HttpRouter router;
    router.setIoContext(io);

    router.add_post_handler(
        "/graphql",
        [executor](Request& req, const http_function&)
            -> net::awaitable<Response>
        {
            auto body = nlohmann::json::parse(req.body(), nullptr, false);
            if (body.is_discarded() || !body.contains("query"))
                co_return make_bad_request_error("Missing query", req.version());

            nlohmann::json vars = body.value("variables", nlohmann::json::object());
            auto result = co_await executor->execute(body["query"], vars);
            co_return make_success_response(result, http::status::ok,
                                            req.version());
        });

    router.add_get_handler(
        "/health",
        [](Request& req, const http_function&) -> Response {
            return make_success_response(
                {{"status", "healthy"}}, http::status::ok, req.version());
        });

    // 6. Start
    TcpStreamType acceptor(io.get_executor(), 8444, ssl);
    HttpServer server(io, acceptor, router);
    io.run();
}
```

---

## Full Walk-through: Inventory GraphQL Server

The diagram below shows exactly what happens for the query:

```graphql
{
  racks {
    id
    name
    servers {
      hostname
      alerts { severity message }
    }
  }
}
```

```
Client
  │
  │  POST /graphql  {"query": "{ racks { id name servers { hostname alerts { severity message } } } }"}
  ▼
HttpRouter → /graphql handler
  │
  │  Parser::parse(query)
  ▼
  Operation {
    selections: [
      FieldSelection { name:"racks",
        selections: [
          FieldSelection { name:"id" },
          FieldSelection { name:"name" },
          FieldSelection { name:"servers",
            selections: [
              FieldSelection { name:"hostname" },
              FieldSelection { name:"alerts",
                selections: [
                  FieldSelection { name:"severity" },
                  FieldSelection { name:"message"  }
                ]
              }
            ]
          }
        ]
      }
    ]
  }
  │
  │  TypedSchema::validateOperation()  → all fields exist ✓
  ▼
  InventoryGraphQLExecutor::resolveRootField("racks", ...)
  │
  │  co_await provider->get("/inventory/racks")
  ▼
  Raw JSON:
  [
    {"rack_id":"R1","name":"Rack-A",
     "servers":[
       {"server_id":"S1","hostname":"web-01","rack_name":"Rack-A",
        "alerts":[{"alertId":"A1","severity":"critical","message":"CPU hot"}]}
     ]}
  ]
  │
  │  projectObject(item, "Rack", selections)
  │    → id   ← rack_id   (scalar)
  │    → name ← name      (scalar)
  │    → servers → projectField(isList=true, returnType="Server")
  │         → projectObject(item, "Server", ...)
  │              → hostname ← hostname (scalar)
  │              → alerts   → projectField(isList=true, returnType="Alert")
  │                   → projectObject(item, "Alert", ...)
  │                        → severity ← severity (scalar)
  │                        → message  ← message  (scalar)
  ▼
  Response:
  {
    "data": {
      "racks": [
        { "id": "R1", "name": "Rack-A",
          "servers": [
            { "hostname": "web-01",
              "alerts": [
                { "severity": "critical", "message": "CPU hot" }
              ]
            }
          ]
        }
      ]
    }
  }
```

---

## API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| `POST` | `/graphql` | Execute a GraphQL query or mutation |
| `GET` | `/health` | Service health check |
| `GET` | `/schema` | Human-readable schema summary (JSON) |

### Request format

```json
{
  "query":     "{ systems { id name } }",
  "variables": { "id": "1" }
}
```

### Response format — success

```json
{
  "data": { "systems": [ { "id": "1", "name": "System-A" } ] }
}
```

### Response format — error

```json
{
  "errors": [ { "message": "Unknown query field: foo" } ]
}
```

---

## File Reference

| File | Role |
|------|------|
| [`include/graphql/ast.hpp`](include/graphql/ast.hpp) | AST node types: `Value`, `Argument`, `FieldSelection`, `Operation` |
| [`include/graphql/parser.hpp`](include/graphql/parser.hpp) | Handwritten lexer + recursive-descent parser |
| [`include/graphql/typed_schema.hpp`](include/graphql/typed_schema.hpp) | `FieldSpec`, `ObjectSpec`, `TypedSchema` |
| [`include/graphql/typed_executor.hpp`](include/graphql/typed_executor.hpp) | `TypedExecutor<Provider>` template base |
| [`include/graphql/util.hpp`](include/graphql/util.hpp) | `argumentsToJson()` and other helpers |
| [`graphql_redfish_schema.cpp`](graphql_redfish_schema.cpp) | Builds the Redfish `TypedSchema` |
| [`graphql_redfish_provider.hpp`](graphql_redfish_provider.hpp) | `RedfishProvider` interface + `HttpRedfishProvider` |
| [`graphql_redfish_executor.cpp`](graphql_redfish_executor.cpp) | `resolveRootField()` for all Redfish queries |
| [`graphql_redfish_server.cpp`](graphql_redfish_server.cpp) | `main()`: wires schema, provider, executor, HTTP server |
| [`meson.build`](meson.build) | Build definition |

---

## Dependencies

| Library | Purpose |
|---------|---------|
| Boost.Asio | Coroutine I/O event loop (`io_context`, `awaitable`) |
| Boost.Beast | HTTP/1.1 protocol layer |
| OpenSSL | TLS/SSL support |
| `nlohmann/json` | JSON parse, serialise, and manipulation |
| coroserver | `HttpServer`, `HttpRouter`, `TcpStreamType`, logging, CLI parsing |

No external GraphQL library is required — the parser and schema engine live entirely in [`include/graphql/`](include/graphql/).

---

## License

Same as the coroserver project.
