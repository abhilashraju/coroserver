# WebClient with stdexec Integration Example

This example demonstrates how to use coroserver's `WebClient` with stdexec (P2300) pipelines for composable HTTP operations.

## Overview

The integration allows you to:
- Convert Boost.Asio awaitables to stdexec senders
- Build type-safe, composable pipelines
- Chain multiple HTTP requests with processing
- Call back into Asio operations from within pipelines
- Handle errors elegantly with fallback strategies

## Requirements

### Required
- C++20 or later
- Boost.Asio
- Boost.Beast
- nlohmann/json
- coroserver library

### Optional (for stdexec integration)
- **stdexec library** (P2300 reference implementation)
  - GitHub: https://github.com/NVIDIA/stdexec
  - Without stdexec, the example will compile but show a message about using WebClient's built-in `.then()` and `.orElse()` methods instead



## Examples

### 1. Functional Web Crawler (webclient_stdexec_example.cpp)

A complete example demonstrating functional programming patterns with stdexec for web crawling.

**Key Patterns:**

1. **Pipeline Composition Pattern**: `just(task) | fetch | extract_links | enqueue`
   ```cpp
   auto pipeline = stdexec::just(*task_opt) |
                   stdexec::let_value(fetch_url(client, executor)) |
                   extract_links(state) |
                   enqueue_links(state);
   ```

2. **Pure Function Transformations**: Each stage is a pure function that transforms data
   - `fetch_url()` - Fetches URL and returns response body
   - `extract_links()` - Extracts links from HTML
   - `enqueue_links()` - Enqueues new tasks from extracted links

3. **State Management**: Shared state pattern with `std::shared_ptr<CrawlerState>`
   - Tracks visited URLs to avoid duplicates
   - Manages BFS queue for crawling
   - Controls crawl depth

4. **Functional Loop Pattern**: `repeat_while(has_work)`
   ```cpp
   while (state->hasMoreWork()) {
       state = co_await process_one_url(client, state, executor);
   }
   ```

5. **Declarative Data Flow**: Each operation clearly expresses intent
   - `dequeue | fetch | extract_links | enqueue`
   - Easy to understand and modify
   - Composable and testable

**Features Demonstrated:**
- BFS web crawling with depth control
- Link extraction using regex
- Duplicate URL detection
- Functional pipeline composition
- Integration of Asio awaitables with stdexec senders
- Error handling in pipelines

**Usage:**
```bash
# Crawl google.com with max depth 2
./webclient_stdexec_example https://google.com 2

# Crawl example.com with max depth 1
./webclient_stdexec_example https://example.com 1
```

### 2. Simple Pipeline
Demonstrates basic pipeline: HTTP GET → extract body → process

```cpp
auto result = co_await to_awaitable(
    to_sender(client.withHost("httpbin.org")
                   .withTarget("/json")
                   .execute<Response>())
    | stdexec::then([](auto tuple) { /* process */ })
    | stdexec::then([](std::string body) { /* transform */ })
);
```

### 2. Chained Requests
Shows how to make multiple HTTP requests in sequence, where each request can depend on the previous one's result.

### 3. Error Handling
Demonstrates error handling with fallback endpoints using `stdexec::let_error`.

### 4. POST with JSON
Shows how to send JSON payloads and process responses.

### 5. Complex Pipeline
Multi-step pipeline with:
- Data fetching
- JSON parsing and validation
- Data transformation
- Conditional requests based on data
- Final processing

### 6. WebClient Built-in Handlers
Demonstrates using WebClient's native `.then()` and `.orElse()` methods within stdexec pipelines.

## Key Concepts

### Converting Awaitables to Senders

```cpp
// WebClient returns awaitable
auto awaitable = client.withHost("example.com").execute<Response>();

// Convert to sender for stdexec pipeline
auto sender = to_sender(awaitable);

// Use in pipeline
auto result = co_await to_awaitable(
    sender | stdexec::then(...) | stdexec::then(...)
);
```

### Calling Asio from Pipeline

Use `stdexec::let_value` to call back into Asio operations:

```cpp
auto result = co_await to_awaitable(
    initial_request_sender
    | stdexec::then([](auto data) { return process(data); })
    | stdexec::let_value([&](auto processed) {
          // Call WebClient again from within pipeline!
          return to_sender(client.withTarget("/next").execute<Response>());
      })
);
```

### Error Handling

```cpp
auto result = co_await to_awaitable(
    risky_operation_sender
    | stdexec::let_error([&](std::exception_ptr) {
          // Fallback operation
          return fallback_sender;
      })
);
```

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Your Application                      │
│                                                          │
│  co_await to_awaitable(                                 │
│      to_sender(webclient.execute())  ← Asio awaitable   │
│      | stdexec::then(...)            ← stdexec pipeline │
│      | stdexec::let_value([&]() {                       │
│            return to_sender(         ← Back to Asio!    │
│                webclient.execute())                      │
│        })                                                │
│  )                                                       │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│              stdexec_asio_adapters.hpp                   │
│                                                          │
│  • to_sender()     - Awaitable → Sender                 │
│  • to_awaitable()  - Sender → Awaitable                 │
│  • asio_scheduler  - Use io_context as scheduler        │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│                  Boost.Asio io_context                   │
│                                                          │
│  All operations run on the same execution context!       │
└─────────────────────────────────────────────────────────┘
```

## Benefits

1. **Composability** - Build complex workflows from simple operations
2. **Type Safety** - Compile-time checked pipelines
3. **Reusability** - Use existing WebClient without modification
4. **Flexibility** - Mix Asio and stdexec operations freely
5. **Performance** - No context switching, all on io_context

## Alternative: WebClient Built-in Composition

If you don't want to use stdexec, WebClient already provides composition via `.then()` and `.orElse()`:

```cpp
auto [ec] = co_await client
    .withHost("example.com")
    .withTarget("/api/data")
    .then([](Response response) -> AwaitableResult<error_code> {
        LOG_INFO("Success: {}", response.body());
        co_return error_code{};
    })
    .orElse([](error_code ec) -> AwaitableResult<error_code> {
        LOG_ERROR("Failed: {}", ec.message());
        co_return ec;
    })
    .execute();
```


## Further Reading

- [P2300 std::execution](https://wg21.link/p2300)
- [stdexec GitHub](https://github.com/NVIDIA/stdexec)
- [Boost.Asio Documentation](https://www.boost.org/doc/libs/release/doc/html/boost_asio.html)
- [coroserver WebClient Documentation](../../include/webclient.hpp)


