# SPDM Design Diagrams

This document contains comprehensive design diagrams for the SPDM (Security Protocol and Data Model) implementation, including architecture, component interactions, and message flows.

## Table of Contents
1. [System Architecture](#system-architecture)
2. [Component Block Diagram](#component-block-diagram)
3. [Certificate Exchange Sequence](#certificate-exchange-sequence)
4. [Measurement Flow Sequence](#measurement-flow-sequence)
5. [Complete SPDM Session Flow](#complete-spdm-session-flow)
6. [Class Diagram](#class-diagram)

---

## System Architecture

### High-Level Architecture Block Diagram

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

---

## Component Block Diagram

### Detailed Component Architecture

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

## Certificate Exchange Sequence

### Certificate Exchange Flow

```mermaid
sequenceDiagram
    participant App as Application
    participant Req as SPDM Requester
    participant CertReq as Cert Exchange<br/>Requester
    participant TCP as TCP Connection
    participant CertResp as Cert Exchange<br/>Handler
    participant Resp as SPDM Responder
    participant TrustStore as Trust Store
    
    App->>Req: exchangeCertificates(certPath)
    activate Req
    
    Note over Req,CertReq: Load Requester Certificate
    Req->>CertReq: loadRequesterCertificate(path, PEM)
    CertReq->>CertReq: Read certificate file
    CertReq-->>Req: Certificate loaded
    
    Note over Req,Resp: PUSH Certificate Phase
    Req->>CertReq: performCertificateExchange()
    activate CertReq
    
    CertReq->>CertReq: Build PUSH_CERTIFICATE request
    CertReq->>TCP: Send PUSH_CERTIFICATE + cert data
    TCP->>CertResp: Forward request
    
    activate CertResp
    CertResp->>CertResp: Validate certificate size
    CertResp->>CertResp: Validate certificate format
    CertResp->>TrustStore: storeCertificate(cert, format, id)
    activate TrustStore
    TrustStore->>TrustStore: Write to file system
    TrustStore-->>CertResp: Storage path
    deactivate TrustStore
    
    CertResp->>TCP: Send PUSH_CERTIFICATE_ACK (SUCCESS)
    TCP->>CertReq: Forward response
    CertReq->>CertReq: Validate ACK status
    
    Note over Req,Resp: PULL Certificate Phase
    CertReq->>CertReq: Build PULL_CERTIFICATE request
    CertReq->>TCP: Send PULL_CERTIFICATE
    TCP->>CertResp: Forward request
    
    CertResp->>CertResp: Get responder certificate
    CertResp->>TCP: Send PULL_CERTIFICATE_ACK + cert data
    deactivate CertResp
    
    TCP->>CertReq: Forward response
    CertReq->>CertReq: Extract certificate from response
    CertReq->>TrustStore: storeCertificate(cert, format, id)
    activate TrustStore
    TrustStore->>TrustStore: Write to file system
    TrustStore-->>CertReq: Storage path
    deactivate TrustStore
    
    CertReq-->>Req: Exchange complete (success, path)
    deactivate CertReq
    Req-->>App: Return (true, certPath)
    deactivate Req
```

---

## Measurement Flow Sequence

### Get Measurements Sequence

```mermaid
sequenceDiagram
    participant App as Application
    participant Req as SPDM Requester
    participant LibSPDM as libspdm
    participant TCP as TCP Connection
    participant Resp as SPDM Responder
    participant Device as Device/TPM
    
    App->>Req: getSignedMeasurements(indices, nonce, slotId)
    activate Req
    
    Note over Req,Resp: Get Certificate Digests
    Req->>LibSPDM: libspdm_get_digest()
    activate LibSPDM
    LibSPDM->>TCP: GET_DIGESTS request
    TCP->>Resp: Forward request
    activate Resp
    Resp->>Device: Retrieve certificate digests
    Device-->>Resp: Digest data
    Resp->>TCP: DIGESTS response
    TCP->>LibSPDM: Forward response
    LibSPDM-->>Req: Slot mask + digests
    deactivate LibSPDM
    
    Note over Req,Resp: Get Certificate Chain
    Req->>LibSPDM: libspdm_get_certificate(slotId)
    activate LibSPDM
    LibSPDM->>TCP: GET_CERTIFICATE request
    TCP->>Resp: Forward request
    Resp->>Device: Retrieve certificate chain
    Device-->>Resp: Certificate data
    Resp->>TCP: CERTIFICATE response
    TCP->>LibSPDM: Forward response
    LibSPDM-->>Req: Certificate chain
    deactivate LibSPDM
    
    Req->>Req: Convert DER to PEM format
    
    Note over Req,Resp: Get Measurements (Loop for each index)
    loop For each measurement index
        Req->>LibSPDM: libspdm_get_measurement(index, slotId)
        activate LibSPDM
        LibSPDM->>TCP: GET_MEASUREMENTS request
        TCP->>Resp: Forward request
        Resp->>Device: Read measurement data
        Device-->>Resp: Measurement blocks
        Resp->>TCP: MEASUREMENTS response (signed)
        TCP->>LibSPDM: Forward response
        LibSPDM->>LibSPDM: Verify signature
        LibSPDM-->>Req: Measurement data + signature
        deactivate LibSPDM
        Req->>Req: Append to measurement buffer
    end
    
    Req->>Req: Base64 encode measurements
    Req->>Req: Get hash & sign algorithms
    
    Req-->>App: Return (hashAlgo, certPEM, measurements, signAlgo, version)
    deactivate Resp
    deactivate Req
```

---

## Complete SPDM Session Flow

### Full SPDM Connection and Attestation Flow

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
    Resp->>LibSPDM: VERSION response
    LibSPDM->>TCP: Forward response
    TCP->>LibSPDM: Receive VERSION
    
    Req->>LibSPDM: GET_CAPABILITIES
    LibSPDM->>TCP: Send GET_CAPABILITIES
    TCP->>Resp: Forward request
    Resp->>LibSPDM: CAPABILITIES response
    LibSPDM->>TCP: Forward response
    TCP->>LibSPDM: Receive CAPABILITIES
    
    Req->>LibSPDM: NEGOTIATE_ALGORITHMS
    LibSPDM->>TCP: Send NEGOTIATE_ALGORITHMS
    TCP->>Resp: Forward request
    Resp->>LibSPDM: ALGORITHMS response
    LibSPDM->>TCP: Forward response
    TCP->>LibSPDM: Receive ALGORITHMS
    
    Req-->>App: Initialization complete
    deactivate Req
    
    Note over App,Resp: Phase 3: Attestation
    App->>Req: getSignedMeasurements(indices, nonce, slotId)
    activate Req
    
    Req->>LibSPDM: GET_DIGESTS
    LibSPDM->>TCP: Send request
    TCP->>Resp: Forward request
    Resp->>TCP: DIGESTS response
    TCP->>LibSPDM: Forward response
    
    Req->>LibSPDM: GET_CERTIFICATE
    LibSPDM->>TCP: Send request
    TCP->>Resp: Forward request
    Resp->>TCP: CERTIFICATE response
    TCP->>LibSPDM: Forward response
    
    loop For each measurement
        Req->>LibSPDM: GET_MEASUREMENTS
        LibSPDM->>TCP: Send request
        TCP->>Resp: Forward request
        Resp->>TCP: MEASUREMENTS response (signed)
        TCP->>LibSPDM: Forward response
        LibSPDM->>LibSPDM: Verify signature
    end
    
    Req-->>App: Measurements + certificate
    deactivate Req

    Note over App,Resp: Phase 4: Certificate Exchange
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
    
    
    
    Note over App,Resp: Phase 5: Provisioning State Update
    App->>Req: setProvisioned(true)
    activate Req
    Req->>TCP: SET_PROVISIONED (custom)
    TCP->>Resp: Forward custom message
    Resp->>DBus: Update provisioned state
    DBus-->>Resp: State updated
    Resp->>TCP: SET_PROVISIONED_ACK
    TCP->>Req: Forward ACK
    Req-->>App: Provisioned state set
    deactivate Req
```

---

## Class Diagram

### Core Classes and Relationships

```mermaid
classDiagram
    class SpdmRequester {
        -SpdmTcpClient client
        -shared_ptr~CertificateTrustStore~ trustStore_
        -unique_ptr~CertificateExchangeRequester~ certExchangeRequester_
        -void* spdmContext
        +SpdmRequester(io_context, trustStorePath)
        +bool connect(host, port)
        +bool init_connection()
        +tuple~bool,string~ exchangeCertificates(certPath)
        +bool setProvisioned(provisioned)
        +optional~GetMeasurementsReturnType~ getSignedMeasurements(indices, nonce, slotId)
        +CertificateExchangeRequester* getCertExchangeRequester()
        -pair~bool,pair~ getSingleMeasurement(index, slotId)
        -CertificateResult getCertificate(slotId)
    }
    
    class SpdmResponder {
        -SpdmTcpServer& server
        -shared_ptr~SpdmResponderObject~ dbusObject
        -CertificateExchangeHandler certExchangeHandler
        -map~uint8_t,CustomMessageHandler~ handlers
        +SpdmResponder(server, dbusObj, trustStorePath)
        +void run()
        +static awaitable~void~ handleSession(self, session)
        -void loadResponderCertificate()
    }
    
    class CertificateExchangeRequester {
        -void* spdmContext_
        -shared_ptr~CertificateTrustStore~ trustStore_
        -string deviceType_
        -vector~uint8_t~ requesterCertificate_
        -CertificateFormat requesterCertFormat_
        -function sendFunction_
        -function receiveFunction_
        +CertificateExchangeRequester(context, trustStore, deviceType)
        +bool loadRequesterCertificate(path, format)
        +bool performCertificateExchange()
        +bool pushCertificateToResponder(cert, format)
        +pair~bool,vector~ pullCertificateFromResponder(format)
        +string getStoredCertificatePath()
        +void setSendFunction(func)
        +void setReceiveFunction(func)
    }
    
    class CertificateExchangeHandler {
        -shared_ptr~CertificateTrustStore~ trustStore_
        -string deviceType_
        -vector~uint8_t~ responderCertificate_
        -CertificateFormat responderCertFormat_
        +CertificateExchangeHandler(trustStore, deviceType)
        +void setResponderCertificate(cert, format)
        +pair~bool,vector~ handlePushCertificate(request, requestSize)
        +pair~bool,vector~ handlePullCertificate(request, requestSize)
        -bool validateCertificate(cert, format)
    }
    
    class CertificateTrustStore {
        <<interface>>
        +tuple~bool,string~ storeCertificate(cert, format, identifier)*
        +vector~uint8_t~ retrieveCertificate(identifier, format)*
        +bool hasCertificate(identifier)*
        +vector~string~ listCertificates()*
        +bool removeCertificate(identifier)*
        +string getCertificatePath(identifier)*
    }
    
    class FileCertificateTrustStore {
        -string storePath_
        +FileCertificateTrustStore(storePath)
        +tuple~bool,string~ storeCertificate(cert, format, identifier)
        +vector~uint8_t~ retrieveCertificate(identifier, format)
        +bool hasCertificate(identifier)
        +vector~string~ listCertificates()
        +bool removeCertificate(identifier)
        +string getCertificatePath(identifier)
        -string getCertificatePath(identifier, format)
    }
    
    class SpdmTcpClient {
        -tcp::socket socket_
        -io_context& io_
        +SpdmTcpClient(io_context)
        +bool connect(host, port)
        +bool send(data, size, timeout)
        +bool receive(buffer, size, timeout)
    }
    
    class SpdmTcpServer {
        -tcp::acceptor acceptor_
        -io_context& io_
        -function sessionHandler_
        +SpdmTcpServer(io_context, port)
        +void accept()
        +void setSessionHandler(handler)
    }
    
    class SpdmSession {
        -tcp::socket socket_
        -io_context& ioContext
        -map~uint8_t,CustomMessageHandler~ customHandlers_
        +SpdmSession(socket, io)
        +void run()
        +void setCustomMessageHandlers(handlers)
        -bool handleCustomMessage(messageCode, request, response)
    }
    
    SpdmRequester --> CertificateExchangeRequester : uses
    SpdmRequester --> SpdmTcpClient : uses
    SpdmRequester --> CertificateTrustStore : uses
    
    SpdmResponder --> CertificateExchangeHandler : uses
    SpdmResponder --> SpdmTcpServer : uses
    SpdmResponder --> SpdmSession : creates
    
    CertificateExchangeRequester --> CertificateTrustStore : uses
    CertificateExchangeHandler --> CertificateTrustStore : uses
    
    FileCertificateTrustStore ..|> CertificateTrustStore : implements
    
    SpdmTcpServer --> SpdmSession : creates
    SpdmSession --> CertificateExchangeHandler : delegates to
```

---

## Data Flow Diagram

### Certificate and Measurement Data Flow

```mermaid
graph LR
    subgraph "Requester Side"
        A[Application] -->|1. Request| B[SPDM Requester]
        B -->|2. Load Cert| C[File System]
        B -->|3. Send| D[TCP Client]
        B -->|8. Store Cert| E[Trust Store]
        B -->|9. Return Data| A
    end
    
    subgraph "Network"
        D <-->|4. TCP/IP| F[TCP Server]
    end
    
    subgraph "Responder Side"
        F -->|5. Dispatch| G[Session Handler]
        G -->|6. Process| H[SPDM Responder]
        H -->|7a. Custom Msg| I[Cert Handler]
        H -->|7b. SPDM Msg| J[libspdm]
        I -->|Store/Retrieve| K[Trust Store]
        J -->|Measurements| L[Device/TPM]
    end
    
    style A fill:#e3f2fd
    style B fill:#bbdefb
    style H fill:#ffe0b2
    style I fill:#ffccbc
    style E fill:#c8e6c9
    style K fill:#c8e6c9
```

---

## State Diagram

### SPDM Session State Machine

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

---

## Notes

### Message Format
All SPDM messages follow the standard SPDM header format:
- **spdm_version**: SPDM protocol version (0x12 for v1.2)
- **request_response_code**: Message type identifier
- **param1, param2**: Message-specific parameters

### Custom Messages
The implementation uses vendor-specific message codes:
- **0x7E**: PUSH_CERTIFICATE / PUSH_CERTIFICATE_ACK
- **0x7F**: PULL_CERTIFICATE / PULL_CERTIFICATE_ACK
- **0x7D**: SET_PROVISIONED / SET_PROVISIONED_ACK

### Security Considerations
1. Certificates are stored in file system with appropriate permissions
2. Certificate validation includes size and format checks
3. Future enhancements should include signature verification and chain validation
4. Transport security (TLS) should be considered for production deployments

### Performance Considerations
1. File I/O operations are synchronous
2. Certificate exchange adds overhead to connection establishment
3. Measurement operations may be time-consuming depending on device
4. Consider caching frequently accessed certificates

---

## References
- DMTF SPDM Specification v1.2
- libspdm Documentation