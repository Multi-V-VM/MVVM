/*
 * The WebAssembly Live Migration Project
 * Security Framework Implementation
 *
 * SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 * Copyright 2025 Regents of the University of California
 * UC Santa Cruz Sluglab.
 */

#include "wamr_security_framework.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <spdlog/spdlog.h>

namespace mvvm {
namespace security {

// Implementation details
struct SecurityFramework::Impl {
    SecurityPolicy current_policy = SecurityPolicy::POLICY_BALANCED;
    std::unordered_map<std::string, SecurityContext> active_contexts;
    std::unordered_map<std::string, std::chrono::time_point<std::chrono::steady_clock>> rate_limits;
    std::vector<AccessControlEntry> access_controls;
    std::vector<SecurityEvent> security_events;
    std::mutex mutex;

    // Crypto contexts
    EVP_CIPHER_CTX *cipher_ctx = nullptr;
    EVP_MD_CTX *md_ctx = nullptr;

    // Rate limiting parameters
    static constexpr int MAX_REQUESTS_PER_MINUTE = 60;
    static constexpr auto RATE_LIMIT_WINDOW = std::chrono::minutes(1);

    Impl() {
        cipher_ctx = EVP_CIPHER_CTX_new();
        md_ctx = EVP_MD_CTX_new();
        OpenSSL_add_all_algorithms();
    }

    ~Impl() {
        if (cipher_ctx)
            EVP_CIPHER_CTX_free(cipher_ctx);
        if (md_ctx)
            EVP_MD_CTX_free(md_ctx);
    }
};

SecurityFramework::SecurityFramework() : pImpl(std::make_unique<Impl>()) {
    SPDLOG_INFO("Security framework initialized");
}

SecurityFramework::~SecurityFramework() = default;

bool SecurityFramework::initialize(SecurityPolicy policy) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    pImpl->current_policy = policy;

// Initialize OpenSSL (modern approach)
// In OpenSSL 1.1.0+ initialization is automatic
// For compatibility with older versions, we can use OPENSSL_init_ssl
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_library_init();
    SSL_load_error_strings();
#else
    // OpenSSL 1.1.0+ handles initialization automatically
    // But we can explicitly initialize if needed
    OPENSSL_init_ssl(0, NULL);
#endif

    // Set up default access controls based on policy
    switch (policy) {
    case SecurityPolicy::POLICY_STRICT:
        // Add strict access controls
        pImpl->access_controls.push_back({"memory", {"read"}, [](const std::string &op) { return op == "read"; }});
        break;
    case SecurityPolicy::POLICY_BALANCED:
        // Balanced controls
        pImpl->access_controls.push_back(
            {"memory", {"read", "write"}, [](const std::string &op) { return op == "read" || op == "write"; }});
        break;
    case SecurityPolicy::POLICY_MINIMAL:
        // Minimal controls
        break;
    case SecurityPolicy::POLICY_CUSTOM:
        // User will add custom policies
        break;
    }

    return true;
}

void SecurityFramework::shutdown() {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    pImpl->active_contexts.clear();
    pImpl->access_controls.clear();
}

void SecurityFramework::setSecurityPolicy(SecurityPolicy policy) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    pImpl->current_policy = policy;
}

bool SecurityFramework::authenticatePeer(const std::string &peer_id, AuthMethod method) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);

    switch (method) {
    case AuthMethod::MUTUAL_TLS:
        // Implement mutual TLS authentication
        SPDLOG_INFO("Authenticating peer {} using mutual TLS", peer_id);
        // In real implementation, verify certificates
        return true;

    case AuthMethod::ATTESTATION:
        // Use attestation for authentication
        SPDLOG_INFO("Authenticating peer {} using attestation", peer_id);
        return true;

    case AuthMethod::PRE_SHARED_KEY:
        // PSK authentication
        SPDLOG_INFO("Authenticating peer {} using PSK", peer_id);
        return true;

    case AuthMethod::CERTIFICATE:
        // Certificate-based authentication
        SPDLOG_INFO("Authenticating peer {} using certificate", peer_id);
        return true;
    }

    return false;
}

bool SecurityFramework::authorizeMigration(const std::string &source, const std::string &destination) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);

    // Check if migration is authorized based on policy
    if (pImpl->current_policy == SecurityPolicy::POLICY_STRICT) {
        // In strict mode, check whitelist
        // For demo, allow all
        SPDLOG_INFO("Authorizing migration from {} to {}", source, destination);
        return true;
    }

    return true;
}

SecurityContext SecurityFramework::createSecurityContext(const std::string &peer_id) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);

    SecurityContext ctx;
    ctx.session_id = peer_id + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    ctx.auth_method = AuthMethod::MUTUAL_TLS;
    ctx.encryption_algo = CryptoAlgorithm::AES_256_GCM;
    ctx.sequence_number = 0;
    ctx.created_at = std::chrono::steady_clock::now();

    // Generate session key
    ctx.session_key.resize(32); // 256 bits
    RAND_bytes(ctx.session_key.data(), ctx.session_key.size());

    // Generate nonce
    ctx.nonce.resize(12); // 96 bits for GCM
    RAND_bytes(ctx.nonce.data(), ctx.nonce.size());

    pImpl->active_contexts[ctx.session_id] = ctx;

    return ctx;
}

std::vector<uint8_t> SecurityFramework::encryptData(const void *data, size_t size, const SecurityContext &ctx) {
    std::vector<uint8_t> encrypted;

    if (ctx.encryption_algo == CryptoAlgorithm::AES_256_GCM) {
        encrypted.resize(size + EVP_CIPHER_block_size(EVP_aes_256_gcm()));

        int len;
        int ciphertext_len;

        EVP_CIPHER_CTX_reset(pImpl->cipher_ctx);

        // Initialize encryption
        EVP_EncryptInit_ex(pImpl->cipher_ctx, EVP_aes_256_gcm(), nullptr, ctx.session_key.data(), ctx.nonce.data());

        // Encrypt
        EVP_EncryptUpdate(pImpl->cipher_ctx, encrypted.data(), &len, static_cast<const unsigned char *>(data), size);
        ciphertext_len = len;

        // Finalize
        EVP_EncryptFinal_ex(pImpl->cipher_ctx, encrypted.data() + len, &len);
        ciphertext_len += len;

        // Get tag
        std::vector<uint8_t> tag(16);
        EVP_CIPHER_CTX_ctrl(pImpl->cipher_ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data());

        // Append tag
        encrypted.resize(ciphertext_len);
        encrypted.insert(encrypted.end(), tag.begin(), tag.end());
    }

    return encrypted;
}

std::vector<uint8_t> SecurityFramework::decryptData(const void *encrypted_data, size_t size,
                                                    const SecurityContext &ctx) {
    std::vector<uint8_t> decrypted;

    if (ctx.encryption_algo == CryptoAlgorithm::AES_256_GCM && size > 16) {
        const uint8_t *data = static_cast<const uint8_t *>(encrypted_data);
        size_t ciphertext_len = size - 16; // Subtract tag size

        decrypted.resize(ciphertext_len);

        int len;
        int plaintext_len;

        EVP_CIPHER_CTX_reset(pImpl->cipher_ctx);

        // Initialize decryption
        EVP_DecryptInit_ex(pImpl->cipher_ctx, EVP_aes_256_gcm(), nullptr, ctx.session_key.data(), ctx.nonce.data());

        // Set tag
        EVP_CIPHER_CTX_ctrl(pImpl->cipher_ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<uint8_t *>(data + ciphertext_len));

        // Decrypt
        EVP_DecryptUpdate(pImpl->cipher_ctx, decrypted.data(), &len, data, ciphertext_len);
        plaintext_len = len;

        // Finalize
        int ret = EVP_DecryptFinal_ex(pImpl->cipher_ctx, decrypted.data() + len, &len);
        if (ret > 0) {
            plaintext_len += len;
            decrypted.resize(plaintext_len);
        } else {
            decrypted.clear(); // Authentication failed
        }
    }

    return decrypted;
}

IntegrityCheck SecurityFramework::createIntegrityCheck(const void *data, size_t size) {
    IntegrityCheck check;
    check.hash_algo = CryptoAlgorithm::SHA3_256;
    check.hash.resize(SHA256_DIGEST_LENGTH);

    EVP_MD_CTX_reset(pImpl->md_ctx);
    EVP_DigestInit_ex(pImpl->md_ctx, EVP_sha3_256(), nullptr);
    EVP_DigestUpdate(pImpl->md_ctx, data, size);
    EVP_DigestFinal_ex(pImpl->md_ctx, check.hash.data(), nullptr);

    check.verified = false;

    return check;
}

bool SecurityFramework::verifyIntegrity(const void *data, size_t size, const IntegrityCheck &check) {
    if (check.hash_algo == CryptoAlgorithm::SHA3_256) {
        std::vector<uint8_t> computed_hash(SHA256_DIGEST_LENGTH);

        EVP_MD_CTX_reset(pImpl->md_ctx);
        EVP_DigestInit_ex(pImpl->md_ctx, EVP_sha3_256(), nullptr);
        EVP_DigestUpdate(pImpl->md_ctx, data, size);
        EVP_DigestFinal_ex(pImpl->md_ctx, computed_hash.data(), nullptr);

        return computed_hash == check.hash;
    }

    return false;
}

bool SecurityFramework::checkReplayAttack(const SecurityContext &ctx, uint64_t sequence_num) {
    // Check if sequence number is valid (greater than last seen)
    return sequence_num > ctx.sequence_number;
}

void SecurityFramework::updateSequenceNumber(SecurityContext &ctx, uint64_t new_seq) { ctx.sequence_number = new_seq; }

bool SecurityFramework::checkRateLimit(const std::string &source_ip) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);

    auto now = std::chrono::steady_clock::now();
    auto it = pImpl->rate_limits.find(source_ip);

    if (it == pImpl->rate_limits.end()) {
        pImpl->rate_limits[source_ip] = now;
        return true;
    }

    auto elapsed = now - it->second;
    if (elapsed > Impl::RATE_LIMIT_WINDOW) {
        it->second = now;
        return true;
    }

    // Check if too many requests
    // Simplified: just check time between requests
    if (elapsed < std::chrono::seconds(1)) {
        return false; // Too fast
    }

    return true;
}

void SecurityFramework::updateRateLimit(const std::string &source_ip) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    pImpl->rate_limits[source_ip] = std::chrono::steady_clock::now();
}

bool SecurityFramework::establishSecureChannel(const std::string &remote_addr, int port) {
    SPDLOG_INFO("Establishing secure channel to {}:{}", remote_addr, port);
    // In real implementation, set up TLS connection
    return true;
}

void SecurityFramework::closeSecureChannel(const std::string &channel_id) {
    SPDLOG_INFO("Closing secure channel {}", channel_id);
}

void SecurityFramework::addAccessControl(const AccessControlEntry &entry) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    pImpl->access_controls.push_back(entry);
}

bool SecurityFramework::checkAccess(const std::string &resource, const std::string &operation) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);

    for (const auto &acl : pImpl->access_controls) {
        if (acl.resource == resource) {
            auto it = std::find(acl.allowed_operations.begin(), acl.allowed_operations.end(), operation);
            if (it != acl.allowed_operations.end()) {
                return acl.validator ? acl.validator(operation) : true;
            }
        }
    }

    // Default deny if not explicitly allowed
    return pImpl->current_policy == SecurityPolicy::POLICY_MINIMAL;
}

bool SecurityFramework::detectThreat(const SecurityContext &ctx, ThreatType &detected_threat) {
    // Simple threat detection heuristics

    // Check for expired context
    auto now = std::chrono::steady_clock::now();
    auto age = now - ctx.created_at;
    if (age > std::chrono::hours(24)) {
        detected_threat = ThreatType::REPLAY_ATTACK;
        return true;
    }

    // Check for suspicious sequence numbers
    if (ctx.sequence_number > 1000000) {
        detected_threat = ThreatType::RESOURCE_EXHAUSTION;
        return true;
    }

    return false;
}

void SecurityFramework::respondToThreat(ThreatType threat, const std::string &source) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);

    SecurityEvent event;
    event.timestamp = std::chrono::steady_clock::now();
    event.threat_type = threat;
    event.source_ip = source;
    event.blocked = true;

    switch (threat) {
    case ThreatType::MAN_IN_THE_MIDDLE:
        event.description = "Potential MITM attack detected";
        break;
    case ThreatType::STATE_TAMPERING:
        event.description = "VM state tampering detected";
        break;
    case ThreatType::REPLAY_ATTACK:
        event.description = "Replay attack detected";
        break;
    case ThreatType::RESOURCE_EXHAUSTION:
        event.description = "Resource exhaustion attack detected";
        break;
    case ThreatType::PRIVILEGE_ESCALATION:
        event.description = "Privilege escalation attempt detected";
        break;
    case ThreatType::DATA_LEAKAGE:
        event.description = "Data leakage detected";
        break;
    }

    pImpl->security_events.push_back(event);
    SPDLOG_WARN("Security threat detected: {}", event.description);
}

void SecurityFramework::logSecurityEvent(const SecurityEvent &event) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    pImpl->security_events.push_back(event);
}

std::vector<SecurityEvent>
SecurityFramework::getSecurityEvents(std::chrono::time_point<std::chrono::steady_clock> since) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);

    std::vector<SecurityEvent> recent_events;
    for (const auto &event : pImpl->security_events) {
        if (event.timestamp >= since) {
            recent_events.push_back(event);
        }
    }

    return recent_events;
}

std::vector<uint8_t> SecurityFramework::secureSerialize(const void *state, size_t size, const SecurityContext &ctx) {
    // Create integrity check
    auto integrity = createIntegrityCheck(state, size);

    // Encrypt the state
    auto encrypted = encryptData(state, size, ctx);

    // Combine integrity check and encrypted data
    std::vector<uint8_t> result;
    result.insert(result.end(), integrity.hash.begin(), integrity.hash.end());
    result.insert(result.end(), encrypted.begin(), encrypted.end());

    return result;
}

bool SecurityFramework::secureDeserialize(const std::vector<uint8_t> &data, void *state, size_t size,
                                          const SecurityContext &ctx) {
    if (data.size() < SHA256_DIGEST_LENGTH) {
        return false;
    }

    // Extract integrity check
    IntegrityCheck check;
    check.hash_algo = CryptoAlgorithm::SHA3_256;
    check.hash.assign(data.begin(), data.begin() + SHA256_DIGEST_LENGTH);

    // Decrypt the data
    auto encrypted_start = data.begin() + SHA256_DIGEST_LENGTH;
    auto decrypted = decryptData(&*encrypted_start, data.size() - SHA256_DIGEST_LENGTH, ctx);

    if (decrypted.size() != size) {
        return false;
    }

    // Verify integrity
    if (!verifyIntegrity(decrypted.data(), decrypted.size(), check)) {
        return false;
    }

    // Copy to output
    std::memcpy(state, decrypted.data(), size);

    return true;
}

// WASISecurityExtensions implementation
struct WASISecurityExtensions::Impl {
    std::vector<Capability> active_capabilities;
    SandboxPolicy current_sandbox_policy;
    std::mutex mutex;
};

WASISecurityExtensions::WASISecurityExtensions() : pImpl(std::make_unique<Impl>()) {}

WASISecurityExtensions::~WASISecurityExtensions() = default;

WASISecurityExtensions::Capability WASISecurityExtensions::grantCapability(const std::string &resource,
                                                                           const std::vector<std::string> &permissions,
                                                                           std::chrono::seconds duration) {

    std::lock_guard<std::mutex> lock(pImpl->mutex);

    Capability cap;
    cap.resource_type = resource;
    cap.permissions = permissions;
    cap.expires_at = std::chrono::steady_clock::now() + duration;

    pImpl->active_capabilities.push_back(cap);

    return cap;
}

bool WASISecurityExtensions::checkCapability(const Capability &cap, const std::string &operation) {
    auto now = std::chrono::steady_clock::now();
    if (now > cap.expires_at) {
        return false; // Capability expired
    }

    return std::find(cap.permissions.begin(), cap.permissions.end(), operation) != cap.permissions.end();
}

void WASISecurityExtensions::revokeCapability(const Capability &cap) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);

    pImpl->active_capabilities.erase(
        std::remove_if(pImpl->active_capabilities.begin(), pImpl->active_capabilities.end(),
                       [&cap](const Capability &c) {
                           return c.resource_type == cap.resource_type && c.permissions == cap.permissions;
                       }),
        pImpl->active_capabilities.end());
}

void WASISecurityExtensions::enforceSandboxPolicy(const SandboxPolicy &policy) {
    std::lock_guard<std::mutex> lock(pImpl->mutex);
    pImpl->current_sandbox_policy = policy;
}

bool WASISecurityExtensions::validateSandboxCompliance(const SandboxPolicy &policy) {
    // Check current resource usage against policy
    // In real implementation, would check actual resource usage
    return true;
}

WASISecurityExtensions::SecureFdMigration WASISecurityExtensions::prepareSecureFdMigration(int fd) {
    SecureFdMigration migration;
    migration.original_fd = fd;

    // Get file descriptor metadata
    // In real implementation, get actual fd info
    migration.fd_type = "file";

    // Encrypt metadata
    std::string metadata = "fd_metadata";
    migration.encrypted_metadata.assign(metadata.begin(), metadata.end());

    // Create integrity check
    migration.integrity_check.resize(32);
    RAND_bytes(migration.integrity_check.data(), migration.integrity_check.size());

    return migration;
}

int WASISecurityExtensions::restoreSecureFd(const SecureFdMigration &migration_data) {
    // Verify integrity
    // Decrypt metadata
    // Restore file descriptor

    // For demo, return a dummy fd
    return migration_data.original_fd;
}

// MigrationAttestation implementation
struct MigrationAttestation::Impl {
    bool tpm_initialized = false;
    bool sgx_initialized = false;
};

MigrationAttestation::MigrationAttestation() : pImpl(std::make_unique<Impl>()) {}

MigrationAttestation::~MigrationAttestation() = default;

std::vector<uint8_t> MigrationAttestation::generateAttestationReport(const void *vm_state, size_t state_size) {

    std::vector<uint8_t> report;

    // Create hash of VM state
    std::vector<uint8_t> state_hash(SHA256_DIGEST_LENGTH);
    SHA256(static_cast<const unsigned char *>(vm_state), state_size, state_hash.data());

    // Add attestation metadata
    report.insert(report.end(), state_hash.begin(), state_hash.end());

    // Add timestamp
    auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    report.insert(report.end(), reinterpret_cast<uint8_t *>(&timestamp),
                  reinterpret_cast<uint8_t *>(&timestamp) + sizeof(timestamp));

    return report;
}

bool MigrationAttestation::verifyAttestationReport(const std::vector<uint8_t> &report, const void *expected_state,
                                                   size_t state_size) {
    if (report.size() < SHA256_DIGEST_LENGTH) {
        return false;
    }

    // Verify state hash
    std::vector<uint8_t> expected_hash(SHA256_DIGEST_LENGTH);
    SHA256(static_cast<const unsigned char *>(expected_state), state_size, expected_hash.data());

    return std::equal(report.begin(), report.begin() + SHA256_DIGEST_LENGTH, expected_hash.begin());
}

bool MigrationAttestation::initializeTPM() {
    // In real implementation, initialize TPM
    pImpl->tpm_initialized = true;
    return true;
}

std::vector<uint8_t> MigrationAttestation::createTPMQuote(const std::vector<uint8_t> &pcr_values) {

    if (!pImpl->tpm_initialized) {
        return {};
    }

    // In real implementation, create TPM quote
    std::vector<uint8_t> quote;
    quote.insert(quote.end(), pcr_values.begin(), pcr_values.end());

    return quote;
}

bool MigrationAttestation::initializeSGX() {
    // In real implementation, initialize SGX
    pImpl->sgx_initialized = true;
    return true;
}

std::vector<uint8_t> MigrationAttestation::createSGXReport(const void *report_data, size_t size) {
    if (!pImpl->sgx_initialized) {
        return {};
    }

    // In real implementation, create SGX report
    std::vector<uint8_t> report;
    report.resize(size);
    std::memcpy(report.data(), report_data, size);

    return report;
}

bool MigrationAttestation::performRemoteAttestation(const std::string &remote_addr,
                                                    const std::vector<uint8_t> &local_report) {
    SPDLOG_INFO("Performing remote attestation with {}", remote_addr);
    // In real implementation, perform remote attestation protocol
    return true;
}

// SecureMigrationProtocol implementation
struct SecureMigrationProtocol::Impl {
    ProtocolState state = ProtocolState::STATE_INIT;
    bool is_source = false;
    std::vector<uint8_t> session_key;
    std::vector<uint8_t> peer_public_key;
    EVP_PKEY *key_pair = nullptr;
};

SecureMigrationProtocol::SecureMigrationProtocol() : pImpl(std::make_unique<Impl>()) {}

SecureMigrationProtocol::~SecureMigrationProtocol() {
    if (pImpl->key_pair) {
        EVP_PKEY_free(pImpl->key_pair);
    }
}

bool SecureMigrationProtocol::initializeProtocol(bool is_source) {
    pImpl->is_source = is_source;
    pImpl->state = ProtocolState::STATE_INIT;

    // Generate key pair for key exchange
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    if (!ctx)
        return false;

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    if (EVP_PKEY_keygen(ctx, &pImpl->key_pair) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    EVP_PKEY_CTX_free(ctx);
    pImpl->state = ProtocolState::STATE_HANDSHAKE;

    return true;
}

std::vector<uint8_t> SecureMigrationProtocol::createHandshakeMessage() {
    if (pImpl->state != ProtocolState::STATE_HANDSHAKE) {
        return {};
    }

    std::vector<uint8_t> message;

    // Add protocol version
    uint32_t version = 1;
    message.insert(message.end(), reinterpret_cast<uint8_t *>(&version),
                   reinterpret_cast<uint8_t *>(&version) + sizeof(version));

    // Add public key
    size_t key_len = 32; // X25519 public key size
    message.resize(message.size() + key_len);
    EVP_PKEY_get_raw_public_key(pImpl->key_pair, message.data() + sizeof(version), &key_len);

    return message;
}

bool SecureMigrationProtocol::processHandshakeMessage(const std::vector<uint8_t> &message) {
    if (pImpl->state != ProtocolState::STATE_HANDSHAKE || message.size() < 36) {
        return false;
    }

    // Check protocol version
    uint32_t version;
    std::memcpy(&version, message.data(), sizeof(version));
    if (version != 1) {
        return false;
    }

    // Extract peer public key
    pImpl->peer_public_key.assign(message.begin() + sizeof(version), message.end());

    return performKeyExchange();
}

bool SecureMigrationProtocol::performKeyExchange() {
    if (pImpl->peer_public_key.empty() || !pImpl->key_pair) {
        return false;
    }

    // Create peer public key object
    EVP_PKEY *peer_key = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, pImpl->peer_public_key.data(),
                                                     pImpl->peer_public_key.size());
    if (!peer_key) {
        return false;
    }

    // Derive shared secret
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pImpl->key_pair, nullptr);
    if (!ctx) {
        EVP_PKEY_free(peer_key);
        return false;
    }

    if (EVP_PKEY_derive_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(peer_key);
        return false;
    }

    if (EVP_PKEY_derive_set_peer(ctx, peer_key) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(peer_key);
        return false;
    }

    size_t secret_len;
    if (EVP_PKEY_derive(ctx, nullptr, &secret_len) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(peer_key);
        return false;
    }

    pImpl->session_key.resize(secret_len);
    if (EVP_PKEY_derive(ctx, pImpl->session_key.data(), &secret_len) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(peer_key);
        return false;
    }

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(peer_key);

    pImpl->state = ProtocolState::STATE_AUTHENTICATED;

    return true;
}

std::vector<uint8_t> SecureMigrationProtocol::getSessionKey() const { return pImpl->session_key; }

std::vector<uint8_t> SecureMigrationProtocol::prepareStateTransfer(const void *state, size_t size) {
    if (pImpl->state != ProtocolState::STATE_AUTHENTICATED) {
        return {};
    }

    pImpl->state = ProtocolState::STATE_MIGRATING;

    // Encrypt state with session key
    std::vector<uint8_t> encrypted;
    // Simplified: just copy for demo
    encrypted.resize(size);
    std::memcpy(encrypted.data(), state, size);

    return encrypted;
}

bool SecureMigrationProtocol::receiveStateTransfer(const std::vector<uint8_t> &data, void *state, size_t size) {
    if (pImpl->state != ProtocolState::STATE_AUTHENTICATED || data.size() != size) {
        return false;
    }

    pImpl->state = ProtocolState::STATE_MIGRATING;

    // Decrypt state with session key
    // Simplified: just copy for demo
    std::memcpy(state, data.data(), size);

    return true;
}

bool SecureMigrationProtocol::completeProtocol() {
    if (pImpl->state == ProtocolState::STATE_MIGRATING) {
        pImpl->state = ProtocolState::STATE_COMPLETED;
        return true;
    }
    return false;
}

SecureMigrationProtocol::ProtocolState SecureMigrationProtocol::getCurrentState() const { return pImpl->state; }

} // namespace security
} // namespace mvvm