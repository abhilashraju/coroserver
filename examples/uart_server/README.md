# UART Server Example

This example demonstrates how to create a UART server using the coroserver library patterns. The server provides asynchronous UART communication with configurable parameters.

## Features

- **Asynchronous I/O**: Uses Boost.Asio for non-blocking UART operations
- **Configurable Parameters**: Supports baud rate, parity, stop bits, and data bits configuration
- **Echo Server**: Echoes received data back to the UART device
- **Logging**: Comprehensive logging of all UART operations
- **Graceful Shutdown**: Handles SIGINT and SIGTERM signals

## Architecture

The implementation follows the coroserver patterns:

1. **uart_server.hpp**: Header file providing:
   - `UartConfig`: Configuration structure for UART parameters
   - `UartDevice`: Async UART device wrapper with read/write operations
   - `UartServer`: Server template that manages UART communication with a router pattern

2. **uart_server.cpp**: Example application demonstrating:
   - Command-line argument parsing
   - UART configuration
   - Router implementation for echo functionality
   - Signal handling for graceful shutdown

## Building

The UART server is built as part of the coroserver examples:

```bash
cd public/sources/coroserver
meson setup builddir
meson compile -C builddir
```

The executable will be located at: `builddir/examples/uart_server/uart_server`

## Usage

### Basic Usage

```bash
./uart_server --device /dev/ttyS0 --baud 115200
```

### Command-Line Options

- `--device, -d`: UART device path (default: `/dev/ttyS0`)
- `--baud, -b`: Baud rate (default: `115200`)
  - Supported rates: 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400
- `--parity, -p`: Parity setting (`even` or `odd`, default: none)
- `--stopbits, -s`: Number of stop bits (1 or 2, default: 1)
- `--databits, -D`: Number of data bits (5, 6, 7, or 8, default: 8)

### Examples

1. **Standard configuration**:
   ```bash
   ./uart_server --device /dev/ttyS0 --baud 115200
   ```

2. **With parity**:
   ```bash
   ./uart_server --device /dev/ttyUSB0 --baud 9600 --parity even
   ```

3. **Custom configuration**:
   ```bash
   ./uart_server --device /dev/ttyS1 --baud 38400 --stopbits 2 --databits 7
   ```

## Testing

### Using socat (Virtual Serial Ports)

Create a pair of virtual serial ports for testing:

```bash
# Terminal 1: Create virtual serial port pair
socat -d -d pty,raw,echo=0 pty,raw,echo=0
# Note the device paths (e.g., /dev/pts/3 and /dev/pts/4)

# Terminal 2: Run uart_server
./uart_server --device /dev/pts/3 --baud 115200

# Terminal 3: Send data to the other end
echo "Hello UART" > /dev/pts/4
cat /dev/pts/4
```

### Using minicom

```bash
# Configure minicom for the UART device
minicom -D /dev/ttyS0 -b 115200

# Type messages to test the echo functionality
```

## Integration with OpenBMC Console Server

This UART server can be integrated with the OpenBMC console server architecture:

1. **Console Multiplexing**: Multiple clients can connect to the UART through network sockets
2. **Data Buffering**: Ringbuffer pattern for efficient data handling
3. **Handler System**: Pluggable handlers for different data processing needs

### Example Integration

```cpp
// Custom router for console server integration
auto consoleRouter = [&ringbuffer](UartDevice& uart) -> net::awaitable<void> {
    std::array<char, 4096> buffer;
    
    while (uart.isOpen()) {
        auto [ec, bytesRead] = co_await uart.read(net::buffer(buffer));
        
        if (ec) break;
        
        // Queue data to ringbuffer for client distribution
        ringbuffer.queue(buffer.data(), bytesRead);
    }
};

UartServer server(io_context, config, consoleRouter);
```

## Code Structure

```
uart_server.hpp
├── UartConfig          # Configuration structure
├── UartDevice          # Async UART device wrapper
│   ├── open()         # Open and configure UART
│   ├── close()        # Close UART device
│   ├── read()         # Async read operation
│   └── write()        # Async write operation
└── UartServer         # Server template
    ├── start()        # Start server
    ├── stop()         # Stop server
    └── handleUart()   # Main coroutine
```

## Error Handling

The server handles various error conditions:

- **Device Open Failures**: Logs error and returns error code
- **Read/Write Errors**: Logs error and gracefully terminates
- **Signal Handling**: Catches SIGINT/SIGTERM for clean shutdown
- **Configuration Errors**: Validates and logs invalid parameters

## Performance Considerations

- **Non-blocking I/O**: All operations are asynchronous
- **Buffer Size**: Default 1024 bytes, adjustable for your needs
- **Timeout Support**: Can be added using Boost.Asio timers
- **Flow Control**: Hardware flow control (RTS/CTS) can be enabled

## Future Enhancements

Potential improvements:

1. **Network Bridge**: Forward UART data to TCP/WebSocket clients
2. **Data Logging**: Log all UART traffic to file
3. **Protocol Handlers**: Add support for specific protocols (Modbus, etc.)
4. **Multiple UARTs**: Support multiple UART devices simultaneously
5. **Configuration File**: Load settings from JSON/YAML file

## Related Files

- `ibm/sources/obmc-console/console-server.c`: OpenBMC console server implementation
- `public/sources/coroserver/include/file_io.hpp`: File I/O patterns
- `public/sources/coroserver/include/socket_streams.hpp`: Socket streaming patterns
- `public/sources/coroserver/examples/tcp_server/`: TCP server example

## License

This code follows the same license as the coroserver library (Apache 2.0).