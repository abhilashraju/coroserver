# OBMC Console Server and Client

A modern C++20 implementation of OpenBMC console server and client using coroutines and the coroserver framework.

## Overview

This implementation provides functionality similar to the original `obmc-console` project but uses modern C++ coroutines and async I/O patterns from the coroserver library.

### Components

1. **console_server** - Server that bridges UART/serial devices to Unix domain sockets
2. **console_client** - Client that connects to the console server for interactive access

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Console Server                          │
│                                                             │
│  ┌──────────────┐      ┌──────────────┐                   │
│  │ UART Device  │◄────►│  RingBuffer  │                   │
│  │ /dev/ttyS0   │      │   (128KB)    │                   │
│  └──────────────┘      └──────┬───────┘                   │
│                               │                             │
│                               ▼                             │
│                    ┌─────────────────────┐                 │
│                    │  Unix Socket Server │                 │
│                    │  /tmp/console.sock  │                 │
│                    └──────────┬──────────┘                 │
└───────────────────────────────┼──────────────────────────────┘
                                │
                    ┌───────────┴───────────┐
                    │                       │
            ┌───────▼────────┐      ┌──────▼────────┐
            │ Console Client │      │ Console Client│
            │   (Session 1)  │      │  (Session 2)  │
            └────────────────┘      └───────────────┘
```

### Key Features

- **Async I/O**: Uses Boost.Asio coroutines for efficient async operations
- **Multiple Clients**: Supports multiple simultaneous client connections
- **Console History**: 128KB ringbuffer stores recent console output
- **Raw Mode**: Terminal set to raw mode for proper console interaction
- **Escape Sequences**: SSH-style escape sequence (Enter ~ .) to disconnect
- **Signal Handling**: Graceful shutdown on SIGINT/SIGTERM

## Building

### Prerequisites

- C++20 compatible compiler (GCC 10+, Clang 12+)
- Boost libraries (1.75+)
- Meson build system
- coroserver library headers

### Build Instructions

```bash
cd public/sources/coroserver/examples/obmc_console
meson setup build
meson compile -C build
```

## Usage

### Console Server

The console server requires a configuration file as its only argument:

```bash
# Start server with configuration file
./build/console_server /etc/obmc-console.conf

# Or with a local config file
./build/console_server ./obmc-console.conf
```

**Usage:**
```
console_server <config-file>
```

All settings are read from the configuration file. No command line options are supported.

### Configuration File

The configuration file uses a simple `key = value` format:

```ini
# OBMC Console Server Configuration

# LPC address for the UART device
lpc-address = 0x3f8

# Serial IRQ number
sirq = 4

# Local TTY baud rate
local-tty-baud = 115200

# Log buffer size (supports k, M, G suffixes)
logsize = 256k

# Log file path
logfile = /var/log/obmc-console.log

# UART device path
device = /dev/ttyS0

# Unix socket path
socket-path = /tmp/obmc-console.sock
```

See [`obmc-console.conf`](obmc-console.conf) for a complete example.

**Supported Baud Rates:**
- 9600, 19200, 38400, 57600, 115200, 230400

### Console Client

Connect to the console server:

```bash
# Basic usage
./build/console_client

# Specify socket path
./build/console_client --socket /tmp/console.sock

# Short option
./build/console_client -s /tmp/console.sock
```

**Options:**
- `--socket, -s`: Unix socket path (default: `/tmp/obmc-console.sock`)

**Escape Sequence:**
To disconnect from the console, press: `Enter ~ .` (newline, tilde, dot)

## Example Session

### Terminal 1: Start Server
```bash
$ ./build/console_server /etc/obmc-console.conf
[INFO] Loaded configuration from: /etc/obmc-console.conf
[INFO] Starting console server
[INFO]   UART device: /dev/ttyS0
[INFO]   Unix socket: /tmp/obmc-console.sock
[INFO]   Baud rate: 115200
[INFO]   Log size: 256k
[INFO]   Log file: /var/log/obmc-console.log
[INFO] Unix socket created: /tmp/obmc-console.sock
[INFO] UART device opened, starting console server
[INFO] Console server running. Press Ctrl+C to stop.
```

### Terminal 2: Connect Client
```bash
$ ./build/console_client -s /tmp/console.sock
[INFO] Starting console client
[INFO]   Socket: /tmp/console.sock
[INFO] Connected to console server
[INFO] Press Enter ~ . to disconnect
[INFO] Terminal set to raw mode

# You now see the host console output
Ubuntu 22.04.1 LTS hostname ttyS0

hostname login: root
Password: 
Last login: Wed May 7 04:00:00 UTC 2026
root@hostname:~# ls
file1.txt  file2.txt
root@hostname:~# 

# Press Enter ~ . to disconnect
[INFO] Escape sequence detected, disconnecting...
[INFO] Disconnecting...
[INFO] Terminal restored to original mode
[INFO] Console client stopped
```

## Implementation Details

### Console Server

The server implements:

1. **UART Management**
   - Opens and configures UART device with specified baud rate
   - Sets terminal to raw mode for proper serial communication
   - Async read loop for receiving data from UART

2. **Unix Socket Server**
   - Creates Unix domain socket for client connections
   - Accepts multiple simultaneous client connections
   - Each client gets its own session handler

3. **RingBuffer**
   - 128KB circular buffer stores recent console output
   - New clients receive console history on connection
   - Prevents memory overflow with fixed-size buffer

4. **Data Flow**
   - UART → RingBuffer → All Connected Clients
   - Any Client → UART (input forwarding)

### Console Client

The client implements:

1. **Terminal Management**
   - Saves original terminal settings
   - Sets terminal to raw mode for console interaction
   - Restores terminal on exit

2. **Bidirectional Forwarding**
   - Socket → stdout (console output to user)
   - stdin → Socket (user input to console)

3. **Escape Detection**
   - Monitors for SSH-style escape sequence (Enter ~ .)
   - State machine tracks: Normal → AfterNewline → AfterTilde → Escape
   - Graceful disconnection on escape sequence

## Comparison with Original obmc-console

| Feature | Original obmc-console | This Implementation |
|---------|----------------------|---------------------|
| Language | C | C++20 |
| I/O Model | poll() + callbacks | Coroutines (async/await) |
| Multiple Clients | ✓ | ✓ |
| Console History | ✓ (128KB) | ✓ (128KB) |
| Escape Sequences | ✓ | ✓ |
| Raw Mode | ✓ | ✓ |
| Signal Handling | ✓ | ✓ |
| Code Style | Procedural | Modern C++ |

## Troubleshooting

### Server won't start

**Problem:** `Failed to open UART device`
- Check device path exists: `ls -l /dev/ttyS0`
- Check permissions: `sudo chmod 666 /dev/ttyS0`
- Verify device is not in use: `lsof /dev/ttyS0`

**Problem:** `Failed to create Unix socket`
- Check socket path is writable
- Remove existing socket: `rm /tmp/console.sock`
- Check permissions on directory

### Client can't connect

**Problem:** `Failed to connect to socket`
- Verify server is running
- Check socket path matches server
- Verify socket file exists: `ls -l /tmp/console.sock`

### No console output

**Problem:** Connected but no output visible
- Check UART device is correct
- Verify baud rate matches host system
- Check UART cable/connection
- Try sending data from host: `echo "test" > /dev/ttyS0`

## License

This implementation follows the same Apache 2.0 license as the coroserver framework.

## See Also

- Original obmc-console: https://github.com/openbmc/obmc-console
- Coroserver framework: `public/sources/coroserver/`
- UART server example: `public/sources/coroserver/examples/uart_server/`