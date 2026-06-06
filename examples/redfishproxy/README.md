# Redfish Proxy Server

A high-performance Redfish proxy server built using the coroserver framework. This proxy forwards Redfish API requests to a configured target Redfish server, providing a centralized access point for managing multiple BMC systems.

## Features

- **Dynamic Configuration**: Configure target Redfish server at runtime via REST API
- **Full Redfish Support**: Forwards all HTTP methods (GET, POST, PUT, PATCH, DELETE)
- **Authentication Handling**: Manages authentication tokens with the target server
- **Async I/O**: Built on Boost.Asio for high-performance async operations
- **SSL/TLS Support**: Secure communication with both clients and target servers
- **Systemd Integration**: Easy deployment as a system service

## Building

The proxy is built as part of the coroserver examples:

```bash
meson setup builddir
meson compile -C builddir
```

## Installation

```bash
meson install -C builddir
```

This installs:
- Binary: `/usr/bin/redfishproxy`
- Service file: `/etc/systemd/system/redfishproxy.service`

## Usage

### Starting the Server

The proxy accepts a port number as a command-line argument:

```bash
# Start on default port 8443
redfishproxy -p 8443

# Or use the systemd service
systemctl start redfishproxy
systemctl enable redfishproxy
```

### Configuring the Target Server

Before forwarding requests, configure the target Redfish server. This creates a persistent connection and authenticates with the target server:

```bash
curl -k -X POST https://localhost:8443/redfish/v1/proxy/config \
  -H "Content-Type: application/json" \
  -d '{
    "host": "192.168.1.100",
    "port": "443",
    "protocol": "https",
    "username": "admin",
    "password": "password"
  }'
```

Response (HTTP 201 Created):
```json
{
  "status": "configured",
  "target": "https://192.168.1.100:443",
  "message": "Authentication successful"
}
```

The response includes an `X-Auth-Token` header containing the authentication token obtained from the target server, similar to Redfish session creation.

**Important**: The proxy creates a single RedfishClient instance during configuration that is reused for all subsequent requests. This improves performance by avoiding repeated authentication.

### Making Redfish Requests

Once configured, all Redfish requests are forwarded to the target server:

```bash
# Get service root
curl -k https://localhost:8443/redfish/v1

# Get systems
curl -k https://localhost:8443/redfish/v1/Systems

# Get specific system
curl -k https://localhost:8443/redfish/v1/Systems/1

# POST request example
curl -k -X POST https://localhost:8443/redfish/v1/Systems/1/Actions/ComputerSystem.Reset \
  -H "Content-Type: application/json" \
  -d '{"ResetType": "ForceRestart"}'
```

## Configuration Endpoint

### POST /redfish/v1/proxy/config

Configure the target Redfish server.

**Request Body:**
```json
{
  "host": "string (required)",
  "port": "string (optional, default: 443)",
  "protocol": "string (optional, default: https)",
  "username": "string (required)",
  "password": "string (required)"
}
```

**Response:**
- `201 Created`: Configuration successful
  - Response includes `X-Auth-Token` header with the authentication token
  - Body contains configuration status and target URL
- `400 Bad Request`: Invalid JSON or missing required fields
- `401 Unauthorized`: Authentication with target server failed
- `500 Internal Server Error`: Configuration failed

**Note**: Configuring the proxy creates a persistent RedfishClient that authenticates with the target server. The authentication token is cached and reused for all subsequent proxy requests, eliminating the need for repeated authentication.

## Proxy Endpoints

All paths under `/redfish/v1/*` are forwarded to the configured target server:

- `GET /redfish/v1/*` - Forward GET requests
- `POST /redfish/v1/*` - Forward POST requests
- `PUT /redfish/v1/*` - Forward PUT requests
- `PATCH /redfish/v1/*` - Forward PATCH requests
- `DELETE /redfish/v1/*` - Forward DELETE requests

## SSL/TLS Configuration

The proxy expects SSL certificates at:
- Certificate chain: `/etc/ssl/certs/https/server.pem`
- Private key: `/etc/ssl/certs/https/server.pem`

For testing, generate self-signed certificates:

```bash
openssl req -x509 -newkey rsa:4096 -keyout server.pem -out server.pem \
  -days 365 -nodes -subj "/CN=localhost"
sudo mkdir -p /etc/ssl/certs/https
sudo mv server.pem /etc/ssl/certs/https/
```

## Architecture

The proxy uses:
- **HttpServer**: Handles incoming client connections
- **RedfishClient**: Single persistent instance created during configuration, reused for all requests
- **HttpRouter**: Routes requests to appropriate handlers
- **Async I/O**: All operations are non-blocking using coroutines
- **ProxyState**: Global state holding the configured client, SSL context, and authentication token

## Error Handling

The proxy returns appropriate HTTP status codes:

- `503 Service Unavailable`: Proxy not configured
- `401 Unauthorized`: Authentication failed with target server
- `502 Bad Gateway`: Failed to communicate with target server
- `500 Internal Server Error`: Internal proxy error

## Logging

The proxy logs important events:
- Configuration changes
- Authentication attempts
- Request forwarding
- Errors and warnings

Set log level in code or via environment variables.

## Security Considerations

1. **SSL/TLS**: Always use HTTPS in production
2. **Credentials**: Store target server credentials securely
3. **Access Control**: Implement authentication for the configuration endpoint
4. **Network**: Run on a trusted network or behind a firewall
5. **Certificates**: Use proper CA-signed certificates in production

## Troubleshooting

### Proxy not starting
- Check if port 8443 is available
- Verify SSL certificates exist and are readable
- Check systemd logs: `journalctl -u redfishproxy -f`

### Configuration fails
- Verify JSON format is correct
- Ensure all required fields are provided
- Check network connectivity to target server

### Requests fail
- Ensure proxy is configured first
- Verify target server credentials are correct
- Check target server is reachable
- Review proxy logs for detailed error messages

## Example Use Cases

1. **Centralized Management**: Single entry point for multiple BMC systems
2. **Load Balancing**: Distribute requests across multiple target servers
3. **Monitoring**: Log and analyze all Redfish API traffic
4. **Security**: Add authentication/authorization layer
5. **Testing**: Mock Redfish server responses for development

## License

MIT License - See LICENSE file for details