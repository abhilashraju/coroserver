# I2C D-Bus Service

A coroutine-based D-Bus service for I2C device access using the `I2CClient` class.

## Overview

This service exposes D-Bus methods to read and write I2C devices asynchronously using C++20 coroutines and Boost.Asio.

## D-Bus Interface

- **Service Name**: `com.ibm.I2CService`
- **Object Path**: `/com/ibm/i2c`
- **Interface**: `com.ibm.I2C`

## Methods

### Write
Write data to an I2C device.

**Signature**: `Write(s device_path, q slave_addr, ay data) -> i`

**Parameters**:
- `device_path` (string): I2C device path (e.g., "/dev/i2c-1")
- `slave_addr` (uint16): I2C slave address (e.g., 0x50)
- `data` (byte array): Data to write

**Returns**:
- `int32`: Number of bytes written, or -1 on error

**Example**:
```bash
busctl call com.ibm.I2CService /com/ibm/i2c com.ibm.I2C Write sqa{y} \
    "/dev/i2c-1" 80 5 0xFF 0x80 0x48 0x65 0x6C
```

### Read
Read data from an I2C device.

**Signature**: `Read(s device_path, q slave_addr, u num_bytes) -> ay`

**Parameters**:
- `device_path` (string): I2C device path
- `slave_addr` (uint16): I2C slave address
- `num_bytes` (uint32): Number of bytes to read

**Returns**:
- `byte array`: Data read from device, empty on error

**Example**:
```bash
busctl call com.ibm.I2CService /com/ibm/i2c com.ibm.I2C Read squ \
    "/dev/i2c-1" 80 6
```

### WriteRead
Write data then read from an I2C device (combined operation).

**Signature**: `WriteRead(s device_path, q slave_addr, ay write_data, u read_bytes) -> ay`

**Parameters**:
- `device_path` (string): I2C device path
- `slave_addr` (uint16): I2C slave address
- `write_data` (byte array): Data to write first
- `read_bytes` (uint32): Number of bytes to read after write

**Returns**:
- `byte array`: Data read from device, empty on error

**Example**:
```bash
busctl call com.ibm.I2CService /com/ibm/i2c com.ibm.I2C WriteRead sqa{y}u \
    "/dev/i2c-1" 80 2 0xFF 0x50 6
```

## Building

The service is built as part of the coroserver examples:

```bash
meson setup build
ninja -C build
```

The executable will be installed to `/usr/bin/i2c-service`.

## Running

```bash
# Run the service
./build/i2c-service

# Or with help
./build/i2c-service --help
```

## Features

- **Coroutine-based**: Uses C++20 coroutines for async I2C operations
- **Automatic retry**: Configurable retry logic with exponential backoff
- **Client caching**: Reuses I2C client instances for the same device/address
- **Error handling**: Comprehensive error reporting via logs
- **D-Bus integration**: Standard D-Bus interface for easy integration

## Architecture

```
┌─────────────────┐
│  D-Bus Clients  │
│  (busctl, etc)  │
└────────┬────────┘
         │ D-Bus calls
         ▼
┌─────────────────────┐
│  I2CServiceManager  │
│  - writeI2C()       │
│  - readI2C()        │
│  - writeReadI2C()   │
└────────┬────────────┘
         │ Uses
         ▼
┌─────────────────────┐
│    I2CClient        │
│  - Coroutine I/O    │
│  - Retry logic      │
│  - Error handling   │
└────────┬────────────┘
         │ Linux I2C
         ▼
┌─────────────────────┐
│   /dev/i2c-*        │
│  (I2C Hardware)     │
└─────────────────────┘
```

## Use Cases

1. **Panel Communication**: Read/write to LCD panels or operator panels
2. **Sensor Access**: Read temperature, voltage, or other sensor data
3. **EEPROM Programming**: Read/write EEPROM devices
4. **Device Configuration**: Configure I2C-based devices
5. **Firmware Updates**: Update firmware on I2C-connected microcontrollers

## Example: LCD Panel Display

```bash
# Display "Hello" on an LCD panel at address 0x50
# Command: 0xFF80 (Display command) + "Hello" (padded to 80 chars per line)
busctl call com.ibm.I2CService /com/ibm/i2c com.ibm.I2C Write sqa{y} \
    "/dev/i2c-1" 80 163 \
    0xFF 0x80 \
    0x48 0x65 0x6C 0x6C 0x6F $(printf ' %.0s' {1..75}) \
    $(printf ' %.0s' {1..80})
```

## Logging

The service uses the reactor logging framework. Logs include:
- I2C operation success/failure
- Client creation and management
- Error details with errno information

## Security Considerations

- Requires appropriate permissions to access `/dev/i2c-*` devices
- Consider running with appropriate user/group permissions
- Validate input parameters to prevent unauthorized device access

## Related Files

- [`i2c_client.hpp`](../../include/i2c_client.hpp) - Coroutine-based I2C client
- [`i2c_service.cpp`](i2c_service.cpp) - D-Bus service implementation