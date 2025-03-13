#ifndef S2N_ATTESTATION_WRAPPER_H
#define S2N_ATTESTATION_WRAPPER_H

#include <stdint.h>

// 前向声明 s2n connection 类型
struct s2n_connection;

// Define attestation types that match the s2n implementation
typedef enum {
    S2N_ATTESTATION_NONE_WRAPPER = 0,
    S2N_ATTESTATION_SGX_WRAPPER = 1,
    S2N_ATTESTATION_TZ_WRAPPER = 2,
    S2N_ATTESTATION_CCA_WRAPPER = 3,
    S2N_ATTESTATION_SEV_WRAPPER = 4,
    S2N_ATTESTATION_TDX_WRAPPER = 5,
    S2N_ATTESTATION_MAX_WRAPPER
} s2n_attestation_type_wrapper;

// 使用简单的布尔返回值而不是 S2N_RESULT
// Wrapper functions to avoid including the actual s2n attestation header
int s2n_connection_set_attestation_type_wrapper(struct s2n_connection *conn, s2n_attestation_type_wrapper type);
int s2n_connection_get_attestation_type_wrapper(struct s2n_connection *conn, s2n_attestation_type_wrapper *type);
int s2n_connection_set_attestation_evidence_wrapper(struct s2n_connection *conn, const uint8_t *evidence,
                                                    uint32_t size);
int s2n_connection_get_peer_attestation_evidence_wrapper(struct s2n_connection *conn, uint8_t **evidence,
                                                         uint32_t *size);
int s2n_connection_verify_attestation_evidence_wrapper(struct s2n_connection *conn);
int s2n_connection_generate_attestation_evidence_wrapper(struct s2n_connection *conn, const uint8_t *challenge,
                                                         uint32_t challenge_size);

#endif // S2N_ATTESTATION_WRAPPER_H