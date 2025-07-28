/*
 * The WebAssembly Live Migration Project
 * Security Framework for WASI Migration Attacks
 *
 * SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 * Copyright 2025 Regents of the University of California
 * UC Santa Cruz Sluglab.
 */

#ifndef WAMR_SECURITY_FRAMEWORK_H
#define WAMR_SECURITY_FRAMEWORK_H

#include "wamr_attestation.h"
#include "wamr_type.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
struct WriteStream;
struct ReadStream;

namespace mvvm {
namespace security {

// Security threat types during migration
enum class ThreatType {
    MAN_IN_THE_MIDDLE, // Interception during migration
    STATE_TAMPERING, // Modification of VM state
    REPLAY_ATTACK, // Replaying old migration data
    RESOURCE_EXHAUSTION, // DoS via excessive migrations
    PRIVILEGE_ESCALATION, // Gaining unauthorized access
    DATA_LEAKAGE // Exposure of sensitive data
};

// Security policies
enum class SecurityPolicy {
    POLICY_STRICT, // All security measures enabled
    POLICY_BALANCED, // Balance between security and performance
    POLICY_MINIMAL, // Basic security only
    POLICY_CUSTOM // User-defined policy
};

// Cryptographic algorithms
enum class CryptoAlgorithm { AES_256_GCM, CHACHA20_POLY1305, RSA_4096, ED25519, SHA3_256 };

// Authentication methods
enum class AuthMethod { MUTUAL_TLS, ATTESTATION, PRE_SHARED_KEY, CERTIFICATE };

// Security context for migration
struct SecurityContext {
    std::string session_id;
    AuthMethod auth_method;
    CryptoAlgorithm encryption_algo;
    std::vector<uint8_t> session_key;
    std::vector<uint8_t> nonce;
    uint64_t sequence_number;
    std::chrono::time_point<std::chrono::steady_clock> created_at;
};

// Integrity verification
struct IntegrityCheck {
    std::vector<uint8_t> hash;
    CryptoAlgorithm hash_algo;
    std::vector<uint8_t> signature;
    bool verified;
};

// Access control entry
struct AccessControlEntry {
    std::string resource;
    std::vector<std::string> allowed_operations;
    std::function<bool(const std::string &)> validator;
};

// Security event for auditing
struct SecurityEvent {
    std::chrono::time_point<std::chrono::steady_clock> timestamp;
    ThreatType threat_type;
    std::string description;
    std::string source_ip;
    bool blocked;
};

class SecurityFramework {
public:
    SecurityFramework();
    ~SecurityFramework();

    // Initialize security framework
    bool initialize(SecurityPolicy policy);
    void shutdown();

    // Configure security policies
    void setSecurityPolicy(SecurityPolicy policy);
    void addCustomPolicy(const std::string &name, std::function<bool(const SecurityContext &)> validator);

    // Authentication and authorization
    bool authenticatePeer(const std::string &peer_id, AuthMethod method);
    bool authorizeMigration(const std::string &source, const std::string &destination);
    SecurityContext createSecurityContext(const std::string &peer_id);

    // Encryption and decryption
    std::vector<uint8_t> encryptData(const void *data, size_t size, const SecurityContext &ctx);
    std::vector<uint8_t> decryptData(const void *encrypted_data, size_t size, const SecurityContext &ctx);

    // Integrity protection
    IntegrityCheck createIntegrityCheck(const void *data, size_t size);
    bool verifyIntegrity(const void *data, size_t size, const IntegrityCheck &check);

    // Anti-replay protection
    bool checkReplayAttack(const SecurityContext &ctx, uint64_t sequence_num);
    void updateSequenceNumber(SecurityContext &ctx, uint64_t new_seq);

    // Rate limiting and DoS protection
    bool checkRateLimit(const std::string &source_ip);
    void updateRateLimit(const std::string &source_ip);

    // Secure channels
    bool establishSecureChannel(const std::string &remote_addr, int port);
    void closeSecureChannel(const std::string &channel_id);

    // Access control
    void addAccessControl(const AccessControlEntry &entry);
    bool checkAccess(const std::string &resource, const std::string &operation);

    // Threat detection and response
    bool detectThreat(const SecurityContext &ctx, ThreatType &detected_threat);
    void respondToThreat(ThreatType threat, const std::string &source);

    // Auditing and logging
    void logSecurityEvent(const SecurityEvent &event);
    std::vector<SecurityEvent> getSecurityEvents(std::chrono::time_point<std::chrono::steady_clock> since);

    // Secure state serialization
    std::vector<uint8_t> secureSerialize(const void *state, size_t size, const SecurityContext &ctx);
    bool secureDeserialize(const std::vector<uint8_t> &data, void *state, size_t size, const SecurityContext &ctx);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// WASI-specific security extensions
class WASISecurityExtensions {
public:
    WASISecurityExtensions();
    ~WASISecurityExtensions();

    // Capability-based security
    struct Capability {
        std::string resource_type;
        std::vector<std::string> permissions;
        std::chrono::time_point<std::chrono::steady_clock> expires_at;
    };

    // Grant capability
    Capability grantCapability(const std::string &resource, const std::vector<std::string> &permissions,
                               std::chrono::seconds duration);

    // Check capability
    bool checkCapability(const Capability &cap, const std::string &operation);

    // Revoke capability
    void revokeCapability(const Capability &cap);

    // Sandbox restrictions during migration
    struct SandboxPolicy {
        bool allow_network_access;
        bool allow_file_access;
        std::vector<std::string> allowed_paths;
        size_t max_memory_usage;
        size_t max_cpu_time_ms;
    };

    void enforceSandboxPolicy(const SandboxPolicy &policy);
    bool validateSandboxCompliance(const SandboxPolicy &policy);

    // Secure file descriptor migration
    struct SecureFdMigration {
        int original_fd;
        std::string fd_type;
        std::vector<uint8_t> encrypted_metadata;
        std::vector<uint8_t> integrity_check;
    };

    SecureFdMigration prepareSecureFdMigration(int fd);
    int restoreSecureFd(const SecureFdMigration &migration_data);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// Attestation support for secure migration
class MigrationAttestation {
public:
    MigrationAttestation();
    ~MigrationAttestation();

    // Generate attestation report
    std::vector<uint8_t> generateAttestationReport(const void *vm_state, size_t state_size);

    // Verify attestation report
    bool verifyAttestationReport(const std::vector<uint8_t> &report, const void *expected_state, size_t state_size);

    // TPM support
    bool initializeTPM();
    std::vector<uint8_t> createTPMQuote(const std::vector<uint8_t> &pcr_values);

    // SGX support
    bool initializeSGX();
    std::vector<uint8_t> createSGXReport(const void *report_data, size_t size);

    // Remote attestation
    bool performRemoteAttestation(const std::string &remote_addr, const std::vector<uint8_t> &local_report);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// Secure communication protocol
class SecureMigrationProtocol {
public:
    SecureMigrationProtocol();
    ~SecureMigrationProtocol();

    // Protocol states
    enum class ProtocolState { INIT, HANDSHAKE, AUTHENTICATED, MIGRATING, COMPLETED, ERROR };

    // Initialize protocol
    bool initializeProtocol(bool is_source);

    // Handshake
    std::vector<uint8_t> createHandshakeMessage();
    bool processHandshakeMessage(const std::vector<uint8_t> &message);

    // Key exchange
    bool performKeyExchange();
    std::vector<uint8_t> getSessionKey() const;

    // State transfer
    std::vector<uint8_t> prepareStateTransfer(const void *state, size_t size);
    bool receiveStateTransfer(const std::vector<uint8_t> &data, void *state, size_t size);

    // Protocol completion
    bool completeProtocol();
    ProtocolState getCurrentState() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace security
} // namespace mvvm

#endif // WAMR_SECURITY_FRAMEWORK_H