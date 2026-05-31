# SPDM (Security Protocol and Data Model) Implementation

## Overview

This is a comprehensive implementation of the DMTF SPDM (Security Protocol and Data Model) specification for secure device attestation and measurement. The implementation provides both SPDM Requester and Responder components with support for certificate exchange, measurements, and component integrity verification.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Key Features](#key-features)
3. [Design Architecture](#design-architecture)
4. [Components](#components)
5. [Message Flows](#message-flows)
6. [Getting Started](#getting-started)
7. [Configuration](#configuration)
8. [Documentation](#documentation)
9. [Security Considerations](#security-considerations)
10. [Building and Installation](#building-and-installation)

---

## Architecture Overview

The SPDM implementation follows a client-server architecture where:

- **SPDM Requester**: Acts as the client, initiating attestation requests and certificate exchanges
- **SPDM Responder**: Acts as the server, responding to attestation requests and providing measurements
- **libspdm**: Core SPDM protocol library providing standard SPDM message handling
- **Custom Extensions**: Additional certificate exchange and provisioning capabilities

```mermaid
graph TB
    subgraph "Client Side"
        APP[Application]
        REQ[SPDM Requester]
        CERT_REQ[Certificate Exchange]
        TRUST_REQ[Trust Store]
    end
    
    subgraph "Network"
        TCP[TCP/IP Connection]
    end
    
    subgraph "Server Side"
        RESP[SPDM Responder]
        CERT_RESP[Certificate Handler]
        TRUST_RESP[Trust Store]
        DBUS[D-Bus Interface]
    end
    
    subgraph "Core Library"
        LIBSPDM[libspdm]
        CRYPTO[Crypto Operations]
    end
    
    APP --> REQ
    REQ --> CERT_REQ
    REQ --> TRUST_REQ
    REQ --> TCP
    
    TCP --> RESP
    RESP --> CERT_RESP
    RESP --> TRUST_RESP
    RESP --> DBUS
    
    REQ -.->|Uses| LIBSPDM
    RESP -.->|Uses| LIBSPDM
    LIBSPDM --> CRYPTO
    
    style REQ fill:#e1f5ff
    style RESP fill:#fff3e0
    style LIBSPDM fill:#f3e5f5
```

---

## Key Features

### Standard SPDM Operations
- ✅ **Version Negotiation**: SPDM protocol version negotiation (v1.2 support)
- ✅ **Capabilities Exchange**: Device capability discovery and negotiation
- ✅ **Algorithm Negotiation**: Cryptographic algorithm selection
- ✅ **Certificate Management**: Certificate chain retrieval and validation
- ✅ **Measurements**: Secure device measurement collection with signatures
- ✅ **Challenge-Response**: Device authentication via challenge-response

### Custom Extensions
- ✅ **Certificate Exchange**: Bidirectional certificate push/pull operations
- ✅ **Trust Store Management**: File-based certificate storage with DER/PEM support
- ✅ **Provisioning State**: Device provisioning state management
- ✅ **D-Bus Integration**: System integration via D-Bus interfaces
- ✅ **Component Integrity**: GraphQL-based component integrity verification

### Transport & Security
- ✅ **TCP/IP Transport**: Reliable TCP-based communication
- ✅ **Asynchronous I/O**: Boost.Asio-based async operations
- ✅ **Session Management**: Multi-session support with proper cleanup
- ✅ **Error Handling**: Comprehensive error handling and logging

---

## Design Architecture

### System Architecture Diagram

```mermaid
graph TB
    subgraph "SPDM Requester System"
        REQ[SPDM Requester]
        REQ_CERT[Certificate Exchange<br/>Requester]
        REQ_TRUST[Trust Store<br/>File-based]
        REQ_TCP[TCP Client]
        REQ_DBUS[D-Bus Interface]
    end
    
    subgraph "Network Layer"
        TCP[TCP/IP Connection<br/>Port: Configurable]
    end
    
    subgraph "SPDM Responder System"
        RESP[SPDM Responder]
        RESP_CERT[Certificate Exchange<br/>Handler]
        RESP_TRUST[Trust Store<br/>File-based]
        RESP_TCP[TCP Server]
        RESP_DBUS[D-Bus Interface]
        RESP_SESSION[Session Manager]
    end
    
    subgraph "libspdm Library"
        LIBSPDM[libspdm Core]
        CRYPTO[Crypto Operations]
        TRANSPORT[Transport Layer]
    end
    
    REQ --> REQ_CERT
    REQ --> REQ_TCP
    REQ --> REQ_DBUS
    REQ_CERT --> REQ_TRUST
    REQ_TCP --> TCP
    
    TCP --> RESP_TCP
    RESP_TCP --> RESP_SESSION
    RESP_SESSION --> RESP
    RESP --> RESP_CERT
    RESP --> RESP_DBUS
    RESP_CERT --> RESP_TRUST
    
    REQ -.->|Uses| LIBSPDM
    RESP -.->|Uses| LIBSPDM
    LIBSPDM --> CRYPTO
    LIBSPDM --> TRANSPORT
    
    style REQ fill:#e1f5ff
    style RESP fill:#fff3e0
    style LIBSPDM fill:#f3e5f5
    style TCP fill:#e8f5e9
```

### State Machine Diagram

The SPDM session follows a well-defined state machine:

```mermaid
stateDiagram-v2
    [*] --> Disconnected
    
    Disconnected --> Connected: TCP Connect
    Connected --> VersionNegotiated: GET_VERSION
    VersionNegotiated --> CapabilitiesExchanged: GET_CAPABILITIES
    CapabilitiesExchanged --> AlgorithmsNegotiated: NEGOTIATE_ALGORITHMS
    
    AlgorithmsNegotiated --> CertificateExchange: Custom PUSH_CERT
    CertificateExchange --> CertificateExchange: Custom PULL_CERT
    CertificateExchange --> Authenticated: Certificates Exchanged
    
    Authenticated --> MeasurementPhase: GET_DIGESTS
    MeasurementPhase --> MeasurementPhase: GET_CERTIFICATE
    MeasurementPhase --> MeasurementPhase: GET_MEASUREMENTS
    MeasurementPhase --> Provisioned: SET_PROVISIONED
    
    Provisioned --> Authenticated: Continue Operations
    Provisioned --> Disconnected: Close Connection
    
    Connected --> Disconnected: Connection Error
    VersionNegotiated --> Disconnected: Error
    CapabilitiesExchanged --> Disconnected: Error
    AlgorithmsNegotiated --> Disconnected: Error
    CertificateExchange --> Disconnected: Error
    Authenticated --> Disconnected: Error
    MeasurementPhase --> Disconnected: Error
```

### Component Interaction Diagram

```mermaid
graph TB
    subgraph "Application Layer"
        APP[Application/Client]
    end
    
    subgraph "SPDM Requester Components"
        REQ_MAIN[SpdmRequester<br/>Main Class]
        REQ_INIT[Requester Init<br/>Connection Setup]
        REQ_CERT_EX[CertificateExchange<br/>Requester]
        REQ_COMP_INT[Component Integrity<br/>Interface]
        REQ_MEAS[Measurement<br/>Operations]
    end
    
    subgraph "SPDM Responder Components"
        RESP_MAIN[SpdmResponder<br/>Main Class]
        RESP_SESSION[Session Handler]
        RESP_CERT_EX[CertificateExchange<br/>Handler]
        RESP_CUSTOM[Custom Message<br/>Handlers]
        RESP_OBJ[Responder Object<br/>D-Bus]
    end
    
    subgraph "Storage Layer"
        TRUST_STORE[Certificate Trust Store<br/>Interface]
        FILE_STORE[FileCertificateTrustStore<br/>Implementation]
        CERT_FILES[(Certificate Files<br/>PEM/DER)]
    end
    
    subgraph "Transport Layer"
        TCP_CLIENT[SpdmTcpClient]
        TCP_SERVER[SpdmTcpServer]
        BOOST_ASIO[Boost.Asio<br/>I/O Context]
    end
    
    subgraph "libspdm Core"
        SPDM_CTX[SPDM Context]
        SPDM_MSG[Message Processing]
        SPDM_CRYPTO[Cryptographic<br/>Operations]
    end
    
    APP --> REQ_MAIN
    REQ_MAIN --> REQ_INIT
    REQ_MAIN --> REQ_CERT_EX
    REQ_MAIN --> REQ_COMP_INT
    REQ_MAIN --> REQ_MEAS
    REQ_MAIN --> TCP_CLIENT
    
    REQ_CERT_EX --> TRUST_STORE
    TRUST_STORE --> FILE_STORE
    FILE_STORE --> CERT_FILES
    
    TCP_CLIENT --> BOOST_ASIO
    TCP_SERVER --> BOOST_ASIO
    
    TCP_CLIENT -.->|Network| TCP_SERVER
    
    TCP_SERVER --> RESP_SESSION
    RESP_SESSION --> RESP_MAIN
    RESP_MAIN --> RESP_CERT_EX
    RESP_MAIN --> RESP_CUSTOM
    RESP_MAIN --> RESP_OBJ
    
    RESP_CERT_EX --> TRUST_STORE
    
    REQ_MAIN -.->|Uses| SPDM_CTX
    RESP_MAIN -.->|Uses| SPDM_CTX
    SPDM_CTX --> SPDM_MSG
    SPDM_CTX --> SPDM_CRYPTO
    
    style REQ_MAIN fill:#bbdefb
    style RESP_MAIN fill:#ffe0b2
    style TRUST_STORE fill:#c8e6c9
    style SPDM_CTX fill:#e1bee7
```

---

## Components

### Core Components

#### 1. SPDM Requester
- **Purpose**: Client-side SPDM implementation
- **Key Features**:
  - Connection management
  - Certificate exchange initiation
  - Measurement collection
  - Component integrity verification
- **Main Class**: `SpdmRequester`

#### 2. SPDM Responder
- **Purpose**: Server-side SPDM implementation
- **Key Features**:
  - Session management
  - Certificate exchange handling
  - Measurement provision
  - D-Bus integration
- **Main Class**: `SpdmResponder`

#### 3. Certificate Trust Store
- **Purpose**: Certificate storage and retrieval
- **Implementation**: File-based storage
- **Formats**: DER and PEM
- **Interface**: `CertificateTrustStore`
- **Implementation**: `FileCertificateTrustStore`

#### 4. Transport Layer
- **Protocol**: TCP/IP
- **Framework**: Boost.Asio
- **Classes**: `SpdmTcpClient`, `SpdmTcpServer`

---

## Message Flows

### Complete SPDM Session Flow

```mermaid
sequenceDiagram
    participant App as Application
    participant Req as SPDM Requester
    participant LibSPDM as libspdm
    participant TCP as TCP Connection
    participant Resp as SPDM Responder
    participant DBus as D-Bus Interface
    
    Note over App,Resp: Phase 1: Connection Establishment
    App->>Req: connect(host, port)
    Req->>TCP: Establish TCP connection
    TCP->>Resp: Connection accepted
    Resp->>Resp: Create session
    TCP-->>Req: Connected
    Req-->>App: Connection success
    
    Note over App,Resp: Phase 2: SPDM Initialization
    App->>Req: init_connection()
    activate Req
    
    Req->>LibSPDM: GET_VERSION
    LibSPDM->>TCP: Send GET_VERSION
    TCP->>Resp: Forward request
    Resp->>TCP: VERSION response
    TCP->>Req: Receive VERSION
    
    Req->>LibSPDM: GET_CAPABILITIES
    LibSPDM->>TCP: Send GET_CAPABILITIES
    TCP->>Resp: Forward request
    Resp->>TCP: CAPABILITIES response
    TCP->>Req: Receive CAPABILITIES
    
    Req->>LibSPDM: NEGOTIATE_ALGORITHMS
    LibSPDM->>TCP: Send NEGOTIATE_ALGORITHMS
    TCP->>Resp: Forward request
    Resp->>TCP: ALGORITHMS response
    TCP->>Req: Receive ALGORITHMS
    
    Req-->>App: Initialization complete
    deactivate Req
    
    Note over App,Resp: Phase 3: Certificate Exchange
    App->>Req: exchangeCertificates(certPath)
    activate Req
    
    Req->>TCP: PUSH_CERTIFICATE (custom)
    TCP->>Resp: Forward custom message
    Resp->>Resp: Store requester certificate
    Resp->>TCP: PUSH_CERTIFICATE_ACK
    TCP->>Req: Forward ACK
    
    Req->>TCP: PULL_CERTIFICATE (custom)
    TCP->>Resp: Forward custom message
    Resp->>Resp: Load responder certificate
    Resp->>TCP: PULL_CERTIFICATE_ACK + cert
    TCP->>Req: Forward response
    Req->>Req: Store responder certificate
    
    Req-->>App: Exchange complete
    deactivate Req
    
    Note over App,Resp: Phase 4: Attestation
    App->>Req: getSignedMeasurements()
    activate Req
    
    Req->>LibSPDM: GET_DIGESTS
    LibSPDM->>TCP: Send request
    TCP->>Resp: Forward request
    Resp->>TCP: DIGESTS response
    TCP->>Req: Forward response
    
    Req->>LibSPDM: GET_CERTIFICATE
    LibSPDM->>TCP: Send request
    TCP->>Resp: Forward request
    Resp->>TCP: CERTIFICATE response
    TCP->>Req: Forward response
    
    loop For each measurement
        Req->>LibSPDM: GET_MEASUREMENTS
        LibSPDM->>TCP: Send request
        TCP->>Resp: Forward request
        Resp->>TCP: MEASUREMENTS response (signed)
        TCP->>Req: Forward response
        LibSPDM->>LibSPDM: Verify signature
    end
    
    Req-->>App: Measurements + certificate
    deactivate Req
```

### Certificate Exchange Sequence

For detailed certificate exchange flow, see [CERTIFICATE_EXCHANGE_README.md](CERTIFICATE_EXCHANGE_README.md).

### Measurement Collection Sequence

For detailed measurement flow diagrams, see [SPDM_DESIGN_DIAGRAMS.md](SPDM_DESIGN_DIAGRAMS.md).

---

## Getting Started

### Prerequisites

- C++17 or later compiler
- Boost libraries (Asio, System)
- libspdm library
- OpenSSL
- Meson build system
- D-Bus development libraries

### Quick Start

#### 1. Build the Project

```bash
# Configure build
meson setup builddir

# Compile
meson compile -C builddir

# Install
sudo meson install -C builddir
```

#### 2. Start SPDM Responder

```bash
# Start the responder service
sudo systemctl start xyz.openbmc_project.spdm.responder.service

# Check status
sudo systemctl status xyz.openbmc_project.spdm.responder.service
```

#### 3. Run SPDM Requester

```cpp
#include "spdm_requester.hpp"

int main() {
    boost::asio::io_context io;
    
    // Create requester
    SpdmRequester requester(io, "/var/lib/spdm/certs/requester");
    
    // Connect to responder
    if (!requester.connect("localhost", 2323)) {
        return 1;
    }
    
    // Initialize SPDM connection
    if (!requester.init_connection()) {
        return 1;
    }
    
    // Exchange certificates
    auto [success, certPath] = requester.exchangeCertificates(
        "/var/lib/spdm/certs/requester_cert.der");
    
    if (success) {
        std::cout << "Certificate exchange successful\n";
        std::cout << "Responder cert stored at: " << certPath << "\n";
    }
    
    // Get measurements
    auto measurements = requester.getSignedMeasurements(
        {0, 1, 2}, "nonce123", 0);
    
    if (measurements) {
        std::cout << "Measurements retrieved successfully\n";
    }
    
    return 0;
}
```

---

## Configuration

### Default Paths

| Component | Path | Description |
|-----------|------|-------------|
| Responder Trust Store | `/var/lib/spdm/certs/responder` | Responder certificate storage |
| Requester Trust Store | `/var/lib/spdm/certs/requester` | Requester certificate storage |
| Responder Certificate | `/var/lib/spdm/certs/responder_cert.der` | Responder's own certificate |
| Requester Certificate | `/var/lib/spdm/certs/requester_cert.der` | Requester's own certificate |

### Environment Variables

- `SPDM_CERT_STORE_PATH`: Override default trust store path
- `SPDM_CERT_MAX_SIZE`: Override maximum certificate size (default: 4MB)
- `SPDM_PORT`: Override default TCP port (default: 2323)

### Custom Configuration

```cpp
// Custom trust store path
SpdmResponder responder(server, dbusObj, "/custom/cert/path");
SpdmRequester requester(io, "/custom/cert/path");

// Custom port
SpdmTcpServer server(io, 8888);
```

---

## Documentation

### Detailed Documentation

- **[SPDM_DESIGN_DIAGRAMS.md](SPDM_DESIGN_DIAGRAMS.md)**: Comprehensive design diagrams including:
  - System architecture
  - Component block diagrams
  - Sequence diagrams
  - State machines
  - Class diagrams
  - Data flow diagrams

- **[CERTIFICATE_EXCHANGE_README.md](CERTIFICATE_EXCHANGE_README.md)**: Certificate exchange implementation:
  - Message structures
  - Usage examples
  - Security considerations
  - Troubleshooting guide

- **[IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md)**: Implementation details:
  - Files created
  - Design decisions
  - Integration points
  - Testing strategy
  - Future enhancements

### API Documentation

Generate API documentation using Doxygen:

```bash
doxygen Doxyfile
```

---

## Security Considerations

### Current Implementation

✅ **Implemented Security Features**:
- Basic certificate validation (size, format)
- File-based secure storage
- Error handling and logging
- Session management
- Signature verification for measurements

⚠️ **Recommended Enhancements**:
- Certificate chain validation
- Certificate revocation checking (CRL/OCSP)
- Encrypted certificate storage
- Hardware security module (HSM) integration
- Access control and audit logging
- Transport layer security (TLS)

### Best Practices

1. **Certificate Management**:
   - Use strong key sizes (RSA 2048+ or ECC 256+)
   - Implement certificate rotation
   - Store private keys securely (TPM/HSM)
   - Validate certificate chains to trusted roots

2. **Network Security**:
   - Use TLS for transport encryption
   - Implement mutual authentication
   - Restrict network access with firewalls
   - Monitor for suspicious activity

3. **Access Control**:
   - Set proper file permissions (0600 for private keys)
   - Use principle of least privilege
   - Implement role-based access control
   - Audit all security-relevant operations

---

