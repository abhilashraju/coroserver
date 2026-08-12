# graphql_dbus_client

A DBus service that connects to a running `graphql_redfish_server`, subscribes
to its GraphQL subscriptions over SSE, and publishes the results as DBus objects
under `/xyz/openbmc_project/Satellite`.

## Architecture

```
main()
  │  parse --config/-c
  │  sdbusplus::asio::connection::request_name("xyz.openbmc_project.Satellite.GraphqlClient")
  │
  └─► GraphqlDbusBridge::create(ioc, conn, configPath)
        │  loadBridgeConfig()  ← JSON file (host, port, subscriptions[])
        │  sdbusplus::asio::object_server  (owns D-Bus tree)
        │  boost::asio::ssl::context  (TLS 1.2, tlsv12_client)
        │
        │  for each SubscriptionConfig:
        │    isList==false ──► DbusObjectProxy (pre-created before ioc.run())
        │                        add_interface / register_property / initialize
        │    isList==true  ──► DbusObjectRegistry (shell only; proxies created lazily)
        │
        └─► SseSubscriptionClient  (one per subscription)
              │  net::co_spawn → reconnectLoop()
              │    exponential back-off: 5s → 10s → 20s … max 60s
              │
              └─► runStream()
                    WebClient<beast::tcp_stream>
                    GET /graphql/subscribe?query=<url-encoded>&interval=<secs>
                    TLS over TCP  ──────────────────────────────────────────►
                                  ◄──────── chunked SSE stream ─────────────
                    readUntil("\n\n")  →  handleFrame()  →  dispatchEvent()
                         │
                         │  payload.is_object()          payload.is_array()
                         ▼                               ▼
                   DbusObjectProxy                 DbusObjectRegistry
                   .applyUpdate()                  .has(path)?
                   set_property()  ← safe in coro   no → net::post(ioc, create)
                                                    yes→ .applyUpdate()  ← safe in coro


  graphql_redfish_server                    D-Bus objects published
  (satellite BMC)                           (local BMC)
  ┌──────────────────────┐                  ┌─────────────────────────────────────────┐
  │ GET /graphql/subscribe│◄── TLS/HTTP/1.1─│ SseSubscriptionClient × N               │
  │ ?query=…&interval=…  │                  │                                         │
  │                       │─── SSE frames ──►│ scalar sub → DbusObjectProxy (1 object) │
  │ data: {"data":{       │                  │                                         │
  │   "systemStatus":{…} │                  │ list sub   → DbusObjectRegistry          │
  │   "chassisStatus":{…}│                  │               └─ DbusObjectProxy per id  │
  │   [{id,mac,…},…]     │                  │                  (lazy, via net::post)   │
  │ }}\n\n               │                  └─────────────────────────────────────────┘
  └──────────────────────┘                            │
                                                      ▼  D-Bus
                              /xyz/openbmc_project/Satellite/Systems/1
                              /xyz/openbmc_project/Satellite/Chassis/1
                              /xyz/openbmc_project/Satellite/Network/Interfaces/<id>
                              /xyz/openbmc_project/Satellite/Network/Interfaces/<id>  …
```

## SSE Protocol

The server (`graphql_redfish_server`) exposes:

```
GET /graphql/subscribe?query=<url-encoded-subscription>&interval=<secs>
Accept: text/event-stream
```

It responds with a persistent `Transfer-Encoding: chunked` stream of SSE frames:

```
data: {"data":{"systemStatus":{"id":"1","name":"System","powerState":"On",...}}}\n\n
data: {"data":{"systemStatus":{"id":"1","name":"System","powerState":"On",...}}}\n\n
```

`graphql_dbus_client` opens one TLS connection per subscription entry, sends the
GET, then loops reading frames with `TcpClient::streamer().readUntil("\n\n")`.

## Configuration

Config is a JSON file (default installed at `/etc/graphql_dbus_client/satellite_queries.json`):

```json
{
  "host": "localhost",
  "port": "8444",
  "subscriptions": [
    {
      "name": "SystemStatus",
      "dbus_path": "/xyz/openbmc_project/Satellite/Systems/1",
      "dbus_interface": "xyz.openbmc_project.Satellite.ComputerSystem",
      "query": "subscription { systemStatus(id:\"1\") { id name powerState status { health state } } }",
      "interval_seconds": 10,
      "field_map": {
        "id":            "Id",
        "name":          "Name",
        "powerState":    "PowerState",
        "status.health": "StatusHealth",
        "status.state":  "StatusState"
      }
    }
  ]
}
```

| Field              | Description |
|--------------------|-------------|
| `host`             | Hostname of the `graphql_redfish_server` |
| `port`             | Port (default `8444`) |
| `name`             | Human-readable label for logging |
| `dbus_path`        | DBus object path to create |
| `dbus_interface`   | DBus interface name on that object |
| `query`            | Full GraphQL subscription string |
| `interval_seconds` | Forwarded as `?interval=` to the server |
| `field_map`        | Dot-path in JSON result → DBus property name |

The `field_map` dot-path notation resolves nested fields:
- `"status.health"` extracts `payload["status"]["health"]`
- `"processorSummary.count"` extracts `payload["processorSummary"]["count"]`

## DBus Service Name

```
xyz.openbmc_project.Satellite.GraphqlClient
```

## Build

Requires `sdbusplus`. The example is built automatically when `sdbusplus_dep`
is found by meson:

```sh
meson setup build
ninja -C build
```

## Usage

```sh
graphql_dbus_client -c /etc/graphql_dbus_client/satellite_queries.json
```

## Reconnection

Each SSE stream has an independent reconnect loop with exponential back-off
(5 s → 10 s → 20 s … capped at 60 s). All streams run concurrently in the
same `io_context`.
