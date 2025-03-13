#ifndef MVVM_WAMR_ATTESTATION_H
#define MVVM_WAMR_ATTESTATION_H

#include "spdlog/spdlog.h"
#include <cstdint>
#include <vector>

// 前向声明 s2n connection 类型
struct s2n_connection;

// Define attestation types that match the s2n implementation
typedef enum {
    WAMR_ATTESTATION_NONE = 0,
    WAMR_ATTESTATION_SGX = 1,
    WAMR_ATTESTATION_TZ = 2,
    WAMR_ATTESTATION_CCA = 3,
    WAMR_ATTESTATION_SEV = 4,
    WAMR_ATTESTATION_TDX = 5,
    WAMR_ATTESTATION_MAX
} wamr_attestation_type;

#ifdef WAMR_BUILD_ATTESTATION
// Include basic s2n header for s2n_connection type
#include "s2n.h"
// Include our own wrapper for s2n_attestation functions
#include "s2n_attestation_wrapper.h"

// Set attestation type on a connection
inline bool wamr_set_attestation_type(struct s2n_connection *conn, wamr_attestation_type type) {
    if (!conn) {
        SPDLOG_ERROR("Connection is null");
        return false;
    }

    if (s2n_connection_set_attestation_type_wrapper(conn, (s2n_attestation_type_wrapper)type) != 0) {
        SPDLOG_ERROR("Failed to set attestation type");
        return false;
    }

    return true;
}

// Set attestation evidence for verification
inline bool wamr_set_attestation_evidence(struct s2n_connection *conn, const std::vector<uint8_t> &evidence) {
    if (!conn || evidence.empty()) {
        SPDLOG_ERROR("Invalid parameters for set attestation evidence");
        return false;
    }

    if (s2n_connection_set_attestation_evidence_wrapper(conn, evidence.data(), evidence.size()) != 0) {
        SPDLOG_ERROR("Failed to set attestation evidence");
        return false;
    }

    return true;
}

// Generate attestation evidence for a connection
inline std::vector<uint8_t> wamr_generate_attestation_evidence(struct s2n_connection *conn,
                                                               const std::vector<uint8_t> &challenge) {
    if (!conn || challenge.empty()) {
        SPDLOG_ERROR("Invalid parameters for generate attestation evidence");
        return {};
    }

    // Generate attestation evidence using the challenge
    if (s2n_connection_generate_attestation_evidence_wrapper(conn, challenge.data(), challenge.size()) != 0) {
        SPDLOG_ERROR("Failed to generate attestation evidence");
        return {};
    }

    // Get the generated evidence
    uint8_t *evidence = nullptr;
    uint32_t evidence_size = 0;
    if (s2n_connection_get_peer_attestation_evidence_wrapper(conn, &evidence, &evidence_size) != 0 || !evidence ||
        evidence_size == 0) {
        SPDLOG_ERROR("Failed to get attestation evidence");
        return {};
    }

    // Copy evidence to a vector and return
    std::vector<uint8_t> evidence_vec(evidence, evidence + evidence_size);
    return evidence_vec;
}

// Verify attestation evidence
inline bool wamr_verify_attestation_evidence(struct s2n_connection *conn) {
    if (!conn) {
        SPDLOG_ERROR("Connection is null for attestation verification");
        return false;
    }

    if (s2n_connection_verify_attestation_evidence_wrapper(conn) != 0) {
        SPDLOG_ERROR("Attestation evidence verification failed");
        return false;
    }

    return true;
}

#else
// Provide stub implementations when attestation is not enabled

struct s2n_connection; // Forward declaration

// Stub implementations that log warnings and return default values
inline bool wamr_set_attestation_type(struct s2n_connection *conn, wamr_attestation_type type) {
    SPDLOG_WARN("Attestation support is not enabled (WAMR_BUILD_ATTESTATION=0). Ignoring set attestation type.");
    return false;
}

inline bool wamr_set_attestation_evidence(struct s2n_connection *conn, const std::vector<uint8_t> &evidence) {
    SPDLOG_WARN("Attestation support is not enabled (WAMR_BUILD_ATTESTATION=0). Ignoring set attestation evidence.");
    return false;
}

inline std::vector<uint8_t> wamr_generate_attestation_evidence(struct s2n_connection *conn,
                                                               const std::vector<uint8_t> &challenge) {
    SPDLOG_WARN("Attestation support is not enabled (WAMR_BUILD_ATTESTATION=0). Cannot generate attestation evidence.");
    return {};
}

inline bool wamr_verify_attestation_evidence(struct s2n_connection *conn) {
    SPDLOG_WARN("Attestation support is not enabled (WAMR_BUILD_ATTESTATION=0). Cannot verify attestation evidence.");
    return false;
}

#endif // WAMR_BUILD_ATTESTATION

// Helper function to generate a random challenge
inline std::vector<uint8_t> wamr_generate_attestation_challenge(size_t challenge_size = 32) {
    std::vector<uint8_t> challenge(challenge_size);
    for (size_t i = 0; i < challenge_size; i++) {
        challenge[i] = rand() % 256;
    }
    return challenge;
}

#endif // MVVM_WAMR_ATTESTATION_H