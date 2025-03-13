#include "s2n_attestation_wrapper.h"
#include "s2n.h"
#include "tls/extensions/s2n_attestation.h"

int s2n_connection_set_attestation_type_wrapper(struct s2n_connection *conn, s2n_attestation_type_wrapper type) {
    if (!conn || type < S2N_ATTESTATION_NONE_WRAPPER || type >= S2N_ATTESTATION_MAX_WRAPPER) {
        return -1;
    }
    s2n_attestation_type real_type = (s2n_attestation_type)type;
    return s2n_result_is_ok(s2n_connection_set_attestation_type(conn, real_type)) ? 0 : -1;
}

int s2n_connection_get_attestation_type_wrapper(struct s2n_connection *conn, s2n_attestation_type_wrapper *type) {
    if (!conn || !type) {
        return -1;
    }
    s2n_attestation_type real_type;
    if (s2n_result_is_ok(s2n_connection_get_attestation_type(conn, &real_type))) {
        *type = (s2n_attestation_type_wrapper)real_type;
        return 0;
    }
    return -1;
}

int s2n_connection_set_attestation_evidence_wrapper(struct s2n_connection *conn, const uint8_t *evidence,
                                                    uint32_t size) {
    if (!conn || !evidence || size == 0) {
        return -1;
    }
    return s2n_result_is_ok(s2n_connection_set_attestation_evidence(conn, evidence, size)) ? 0 : -1;
}

int s2n_connection_get_peer_attestation_evidence_wrapper(struct s2n_connection *conn, uint8_t **evidence,
                                                         uint32_t *size) {
    if (!conn || !evidence || !size) {
        return -1;
    }
    return s2n_result_is_ok(s2n_connection_get_peer_attestation_evidence(conn, evidence, size)) ? 0 : -1;
}

int s2n_connection_verify_attestation_evidence_wrapper(struct s2n_connection *conn) {
    if (!conn) {
        return -1;
    }
    return s2n_result_is_ok(s2n_connection_verify_attestation_evidence(conn)) ? 0 : -1;
}

int s2n_connection_generate_attestation_evidence_wrapper(struct s2n_connection *conn, const uint8_t *challenge,
                                                         uint32_t challenge_size) {
    if (!conn || !challenge || challenge_size == 0) {
        return -1;
    }
    return s2n_result_is_ok(s2n_connection_generate_attestation_evidence(conn, challenge, challenge_size)) ? 0 : -1;
}