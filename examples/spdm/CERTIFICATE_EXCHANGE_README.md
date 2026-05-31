# SPDM Custom Certificate Exchange Implementation

## Overview

This implementation adds custom SPDM messages to enable certificate exchange between an SPDM requester and responder. The feature allows:

1. **PUSH_CERTIFICATE**: Requester pushes its certificate to the responder
2. **PULL_CERTIFICATE**: Requester pulls the responder's certificate

Both certificates are stored in their respective trust stores for future verification and attestation operations.

## Architecture

### Components

#### 1. Custom Message Definitions (`spdm_custom_messages.hpp`)
- Defines custom SPDM message codes (0x7E for PUSH, 0x7F for PULL)
- Message structures for requests and responses
- Certificate format enumeration (DER/PEM)
- Status codes for certificate exchange operations

#### 2. Certificate Trust Store (`spdm_cert_store.cpp`)
- `CertificateTrustStore`: Abstract interface for certificate storage
- `FileCertificateTrustStore`: File-based implementation
- Operations: store, retrieve, list, remove certificates
- Supports both DER and PEM formats

#### 3. Responder Handler (`cert_exchange_handler.hpp`)
- `CertificateExchangeHandler`: Handles incoming certificate exchange requests
- Validates incoming certificates
- Stores requester certificates in trust store
- Sends responder certificate on PULL requests

#### 4. Requester Client (`cert_exchange_requester.hpp`)
- `CertificateExchangeRequester`: Initiates certificate exchange
- Sends PUSH_CERTIFICATE with requester's certificate
- Sends PULL_CERTIFICATE to retrieve responder's certificate
- Stores received certificates in trust store

#### 5. Integration
- **Responder** (`spdm_responder.hpp`): Extended dispatch loop to handle custom messages
- **Requester** (`spdm_requester.hpp`): Added `exchangeCertificates()` method

## Message Flow

```
Requester                                    Responder
    |                                            |
    |  1. PUSH_CERTIFICATE (with cert)          |
    |------------------------------------------>|
    |                                            | - Validate certificate
    |                                            | - Store in trust store
    |  2. PUSH_CERTIFICATE_ACK (status)         |
    |<------------------------------------------|
    |                                            |
    |  3. PULL_CERTIFICATE (request)            |
    |------------------------------------------>|
    |                                            | - Retrieve responder cert
    |  4. PULL_CERTIFICATE_ACK (with cert)      |
    |<------------------------------------------|
    | - Store in trust store                    |
    |                                            |
```

## Message Structures

### PUSH_CERTIFICATE Request
```c
struct SpdmPushCertificateRequest {
    SpdmMessageHeader header;
    uint32_t cert_size;
    uint8_t cert_format;  // 0=DER, 1=PEM
    uint8_t reserved[3];
    // Followed by certificate data
};
```

### PUSH_CERTIFICATE Response
```c
struct SpdmPushCertificateResponse {
    SpdmMessageHeader header;
    uint8_t status;       // 0=success, 1=invalid cert, 2=storage full
    uint8_t reserved[3];
};
```

### PULL_CERTIFICATE Request
```c
struct SpdmPullCertificateRequest {
    SpdmMessageHeader header;
    uint8_t cert_format;  // Requested format
    uint8_t reserved[3];
};
```

### PULL_CERTIFICATE Response
```c
struct SpdmPullCertificateResponse {
    SpdmMessageHeader header;
    uint8_t status;
    uint32_t cert_size;
    uint8_t cert_format;
    uint8_t reserved[2];
    // Followed by certificate data
};
```

## Usage

### Responder Setup

```cpp
// Create responder with trust store path
std::string trustStorePath = "/var/lib/spdm/certs/responder";
SpdmResponder responder(server, dbusObject, trustStorePath);

// The responder automatically:
// - Loads its certificate from /var/lib/spdm/certs/responder_cert.der
// - Handles incoming PUSH_CERTIFICATE and PULL_CERTIFICATE requests
// - Stores requester certificates in the trust store
```

### Requester Usage

```cpp
// Create requester with trust store path
std::string trustStorePath = "/var/lib/spdm/certs/requester";
SpdmRequester requester(io_context, trustStorePath);

// Connect and initialize SPDM connection
requester.connect(host, port);
requester.sayhello();
requester.init_connection();

// Perform certificate exchange
std::string certPath = "/var/lib/spdm/certs/requester_cert.der";
if (requester.exchangeCertificates(certPath)) {
    LOG_INFO("Certificate exchange successful");
    // Responder's certificate is now in trust store
}

// Advanced usage - manual control
auto* certExchanger = requester.getCertExchangeRequester();

// Load certificate
certExchanger->loadRequesterCertificate(certPath, CertificateFormat::DER);

// Push certificate
certExchanger->pushCertificateToResponder(cert, CertificateFormat::DER);

// Pull certificate
auto [success, responderCert] = certExchanger->pullCertificateFromResponder(
    CertificateFormat::DER);
```

## Trust Store Organization

### Directory Structure
```
/var/lib/spdm/certs/
├── requester/
│   ├── requester_cert.der          # Requester's own certificate
│   ├── responder_<device_id>.der   # Responder certificates
│   └── responder_<device_id>.pem
└── responder/
    ├── responder_cert.der          # Responder's own certificate
    ├── requester_<device_id>.der   # Requester certificates
    └── requester_<device_id>.pem
```

### Certificate Naming Convention
- Own certificates: `requester_cert.der` or `responder_cert.der`
- Peer certificates: `<peer_type>_<device_id>.<format>`
  - Example: `responder_device123.der`
  - Example: `requester_device456.pem`

## Status Codes

| Code | Name | Description |
|------|------|-------------|
| 0x00 | CERT_EXCHANGE_SUCCESS | Operation successful |
| 0x01 | CERT_EXCHANGE_ERROR_INVALID_CERT | Certificate validation failed |
| 0x02 | CERT_EXCHANGE_ERROR_STORAGE_FULL | Trust store is full |
| 0x03 | CERT_EXCHANGE_ERROR_NOT_FOUND | Certificate not found |
| 0x04 | CERT_EXCHANGE_ERROR_INVALID_REQUEST | Malformed request |

## Security Considerations

### Current Implementation
1. **Basic Validation**: Checks certificate size and format
2. **Storage Limits**: Maximum certificate size of 4MB
3. **File-based Storage**: Certificates stored in filesystem

### Recommended Enhancements
1. **Certificate Validation**:
   - Parse and validate certificate structure
   - Verify certificate signature
   - Check validity period (not before/not after)
   - Verify certificate chain to trusted root CA

2. **Access Control**:
   - Implement proper file permissions (0600 for private keys)
   - Use secure storage mechanisms (TPM, HSM)
   - Encrypt certificates at rest

3. **Revocation Checking**:
   - Implement CRL (Certificate Revocation List) checking
   - Support OCSP (Online Certificate Status Protocol)

4. **Mutual Authentication**:
   - Verify requester identity before accepting certificates
   - Implement challenge-response authentication

## Error Handling

### Responder
- Invalid certificate size → CERT_EXCHANGE_ERROR_INVALID_CERT
- Storage failure → CERT_EXCHANGE_ERROR_STORAGE_FULL
- Malformed request → CERT_EXCHANGE_ERROR_INVALID_REQUEST
- No certificate available → CERT_EXCHANGE_ERROR_NOT_FOUND

### Requester
- Connection failure → Returns false, logs error
- Invalid response → Returns false, logs error
- Certificate storage failure → Logs warning, continues

## Testing

### Unit Tests (Recommended)
```cpp
// Test certificate push
TEST(CertExchange, PushCertificate) {
    // Create test certificate
    std::vector<uint8_t> testCert = loadTestCertificate();
    
    // Push to responder
    bool success = requester.pushCertificateToResponder(
        testCert, CertificateFormat::DER);
    
    EXPECT_TRUE(success);
    
    // Verify stored in trust store
    EXPECT_TRUE(trustStore->hasCertificate("requester_test"));
}

// Test certificate pull
TEST(CertExchange, PullCertificate) {
    auto [success, cert] = requester.pullCertificateFromResponder(
        CertificateFormat::DER);
    
    EXPECT_TRUE(success);
    EXPECT_FALSE(cert.empty());
}
```

### Integration Tests
1. Start responder with test certificate
2. Connect requester
3. Perform certificate exchange
4. Verify both certificates in respective trust stores
5. Verify certificates can be retrieved and used

## Configuration

### Environment Variables
- `SPDM_CERT_STORE_PATH`: Override default trust store path
- `SPDM_CERT_MAX_SIZE`: Override maximum certificate size
- `SPDM_CERT_TIMEOUT`: Override certificate exchange timeout

### Build Configuration
Add to meson.build:
```meson
spdm_cert_exchange_sources = files(
    'includes/spdm_cert_store.cpp',
)

spdm_cert_exchange_deps = [
    dependency('filesystem'),
]
```

## Troubleshooting

### Common Issues

1. **Certificate not found**
   - Check file path and permissions
   - Verify certificate format (DER vs PEM)
   - Check trust store directory exists

2. **Exchange timeout**
   - Increase timeout value
   - Check network connectivity
   - Verify responder is running

3. **Invalid certificate error**
   - Verify certificate format
   - Check certificate is not corrupted
   - Ensure certificate size is within limits

4. **Storage failure**
   - Check disk space
   - Verify write permissions
   - Check trust store directory exists

## Future Enhancements

1. **Certificate Chain Support**: Handle full certificate chains
2. **Multiple Certificates**: Support multiple certificates per entity
3. **Certificate Rotation**: Automatic certificate renewal
4. **Encrypted Storage**: Encrypt certificates at rest
5. **Database Backend**: Replace file storage with database
6. **Certificate Policies**: Implement certificate policy enforcement
7. **Audit Logging**: Log all certificate operations
8. **Performance**: Optimize for large certificate chains

## References

- DMTF SPDM Specification: https://www.dmtf.org/standards/spdm
- libspdm: https://github.com/DMTF/libspdm
- X.509 Certificate Format: RFC 5280

## License

This implementation follows the same license as the parent project.

## Authors
- Date: 2025-01-21