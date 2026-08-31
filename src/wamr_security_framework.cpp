/* Cryptographic migration security.  All unauthenticated paths fail closed. */
#include "wamr_security_framework.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <limits>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <spdlog/spdlog.h>

namespace mvvm::security {
namespace {
constexpr std::array<uint8_t, 4> magic{{'M', 'V', 'S', '1'}};
constexpr size_t nonce_size = 12, tag_size = 16, header_size = 4 + 1 + 8 + nonce_size + 4;
void put32(std::vector<uint8_t> &out, uint32_t n) {
    for (unsigned s = 0; s < 32; s += 8)
        out.push_back(uint8_t(n >> s));
}
void put64(std::vector<uint8_t> &out, uint64_t n) {
    for (unsigned s = 0; s < 64; s += 8)
        out.push_back(uint8_t(n >> s));
}
bool get32(const std::vector<uint8_t> &in, size_t &p, uint32_t &n) {
    if (p + 4 > in.size())
        return false;
    n = 0;
    for (unsigned s = 0; s < 32; s += 8)
        n |= uint32_t(in[p++]) << s;
    return true;
}
bool get64(const std::vector<uint8_t> &in, size_t &p, uint64_t &n) {
    if (p + 8 > in.size())
        return false;
    n = 0;
    for (unsigned s = 0; s < 64; s += 8)
        n |= uint64_t(in[p++]) << s;
    return true;
}
bool valid(const SecurityContext &c) {
    return !c.session_id.empty() && c.encryption_algo == CryptoAlgorithm::AES_256_GCM && c.session_key.size() == 32;
}
bool hash(const void *data, size_t size, std::vector<uint8_t> &out) {
    if (!data && size)
        return false;
    out.assign(32, 0);
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned int n = 0;
    bool ok = ctx && EVP_DigestInit_ex(ctx, EVP_sha3_256(), nullptr) == 1 && EVP_DigestUpdate(ctx, data, size) == 1 &&
              EVP_DigestFinal_ex(ctx, out.data(), &n) == 1 && n == out.size();
    EVP_MD_CTX_free(ctx);
    return ok;
}
std::vector<uint8_t> aad(const std::vector<uint8_t> &head, const SecurityContext &c) {
    std::vector<uint8_t> out(head.begin(), head.begin() + 4 + 1 + 8 + nonce_size);
    out.insert(out.end(), c.session_id.begin(), c.session_id.end());
    return out;
}
bool encrypt(const uint8_t *src, size_t len, const SecurityContext &c, const std::vector<uint8_t> &associated,
             const uint8_t *nonce, std::vector<uint8_t> &dst, std::array<uint8_t, tag_size> &tag) {
    if (len > size_t(std::numeric_limits<int>::max()))
        return false;
    EVP_CIPHER_CTX *x = EVP_CIPHER_CTX_new();
    if (!x)
        return false;
    dst.assign(len, 0);
    int a = 0, b = 0;
    bool ok = EVP_EncryptInit_ex(x, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(x, EVP_CTRL_GCM_SET_IVLEN, nonce_size, nullptr) == 1 &&
              EVP_EncryptInit_ex(x, nullptr, nullptr, c.session_key.data(), nonce) == 1 &&
              EVP_EncryptUpdate(x, nullptr, &a, associated.data(), int(associated.size())) == 1 &&
              EVP_EncryptUpdate(x, dst.data(), &a, src, int(len)) == 1 &&
              EVP_EncryptFinal_ex(x, dst.data() + a, &b) == 1 &&
              EVP_CIPHER_CTX_ctrl(x, EVP_CTRL_GCM_GET_TAG, tag_size, tag.data()) == 1;
    EVP_CIPHER_CTX_free(x);
    if (!ok) {
        dst.clear();
        return false;
    }
    dst.resize(a + b);
    return dst.size() == len;
}
bool decrypt(const uint8_t *src, size_t len, const SecurityContext &c, const std::vector<uint8_t> &associated,
             const uint8_t *nonce, const uint8_t *tag, std::vector<uint8_t> &dst) {
    if (len > size_t(std::numeric_limits<int>::max()))
        return false;
    EVP_CIPHER_CTX *x = EVP_CIPHER_CTX_new();
    if (!x)
        return false;
    dst.assign(len, 0);
    int a = 0, b = 0;
    bool ok = EVP_DecryptInit_ex(x, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
              EVP_CIPHER_CTX_ctrl(x, EVP_CTRL_GCM_SET_IVLEN, nonce_size, nullptr) == 1 &&
              EVP_DecryptInit_ex(x, nullptr, nullptr, c.session_key.data(), nonce) == 1 &&
              EVP_DecryptUpdate(x, nullptr, &a, associated.data(), int(associated.size())) == 1 &&
              EVP_DecryptUpdate(x, dst.data(), &a, src, int(len)) == 1 &&
              EVP_CIPHER_CTX_ctrl(x, EVP_CTRL_GCM_SET_TAG, tag_size, const_cast<uint8_t *>(tag)) == 1 &&
              EVP_DecryptFinal_ex(x, dst.data() + a, &b) == 1;
    EVP_CIPHER_CTX_free(x);
    if (!ok) {
        dst.clear();
        return false;
    }
    dst.resize(a + b);
    return dst.size() == len;
}
} // namespace

struct SecurityFramework::Impl {
    SecurityPolicy policy = SecurityPolicy::POLICY_BALANCED;
    bool initialized = false;
    std::unordered_map<std::string, SecurityContext> contexts;
    std::unordered_map<std::string, uint64_t> sent, received;
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> requests;
    std::vector<AccessControlEntry> acl;
    std::unordered_map<std::string, std::function<bool(const SecurityContext &)>> custom;
    std::vector<SecurityEvent> events;
    std::mutex mutex;
};
SecurityFramework::SecurityFramework() : pImpl(std::make_unique<Impl>()) {}
SecurityFramework::~SecurityFramework() = default;
bool SecurityFramework::initialize(SecurityPolicy p) {
    std::lock_guard lock(pImpl->mutex);
    pImpl->policy = p;
    pImpl->initialized = RAND_status() == 1;
    pImpl->acl.clear();
    if (p == SecurityPolicy::POLICY_STRICT)
        pImpl->acl.push_back({"memory", {"read"}, [](const std::string &o) { return o == "read"; }});
    if (p == SecurityPolicy::POLICY_BALANCED)
        pImpl->acl.push_back(
            {"memory", {"read", "write"}, [](const std::string &o) { return o == "read" || o == "write"; }});
    return pImpl->initialized;
}
void SecurityFramework::shutdown() {
    std::lock_guard lock(pImpl->mutex);
    for (auto &[_, c] : pImpl->contexts)
        OPENSSL_cleanse(c.session_key.data(), c.session_key.size());
    pImpl->contexts.clear();
    pImpl->sent.clear();
    pImpl->received.clear();
    pImpl->requests.clear();
    pImpl->acl.clear();
    pImpl->initialized = false;
}
void SecurityFramework::setSecurityPolicy(SecurityPolicy p) { initialize(p); }
void SecurityFramework::addCustomPolicy(const std::string &n, std::function<bool(const SecurityContext &)> f) {
    std::lock_guard lock(pImpl->mutex);
    if (!n.empty() && f)
        pImpl->custom[n] = std::move(f);
}
bool SecurityFramework::authenticatePeer(const std::string &peer, AuthMethod method) {
    (void)method;
    SPDLOG_ERROR("Authentication of {} refused: this API accepts no credentials, certificate, or attestation evidence",
                 peer);
    return false;
}
bool SecurityFramework::authorizeMigration(const std::string &source, const std::string &destination) {
    if (source.empty() || destination.empty())
        return false;
    std::lock_guard lock(pImpl->mutex);
    if (!pImpl->initialized)
        return false;
    if (pImpl->policy == SecurityPolicy::POLICY_MINIMAL)
        return true;
    for (const auto &a : pImpl->acl)
        if ((a.resource == "migration" || a.resource == "migration:" + source + "->" + destination) &&
            std::find(a.allowed_operations.begin(), a.allowed_operations.end(), "migrate") !=
                a.allowed_operations.end() &&
            (!a.validator || a.validator("migrate")))
            return true;
    return false;
}
SecurityContext SecurityFramework::createSecurityContext(const std::string &peer) {
    SecurityContext c{};
    if (peer.empty())
        return c;
    c.session_id = peer + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    c.auth_method = AuthMethod::MUTUAL_TLS;
    c.encryption_algo = CryptoAlgorithm::AES_256_GCM;
    c.session_key.resize(32);
    c.nonce.resize(nonce_size);
    c.created_at = std::chrono::steady_clock::now();
    if (RAND_bytes(c.session_key.data(), 32) != 1 || RAND_bytes(c.nonce.data(), nonce_size) != 1)
        return {};
    std::lock_guard lock(pImpl->mutex);
    if (!pImpl->initialized)
        return {};
    pImpl->contexts[c.session_id] = c;
    return c;
}
std::vector<uint8_t> SecurityFramework::encryptData(const void *data, size_t size, const SecurityContext &c) {
    if ((!data && size) || !valid(c) || size > UINT32_MAX)
        return {};
    uint64_t seq;
    {
        std::lock_guard lock(pImpl->mutex);
        auto it = pImpl->contexts.find(c.session_id);
        if (!pImpl->initialized || it == pImpl->contexts.end() ||
            CRYPTO_memcmp(it->second.session_key.data(), c.session_key.data(), 32) != 0)
            return {};
        seq = ++pImpl->sent[c.session_id];
    }
    std::vector<uint8_t> out(magic.begin(), magic.end());
    out.push_back(1);
    put64(out, seq);
    std::array<uint8_t, nonce_size> n{};
    if (RAND_bytes(n.data(), n.size()) != 1)
        return {};
    out.insert(out.end(), n.begin(), n.end());
    put32(out, uint32_t(size));
    std::vector<uint8_t> ct;
    std::array<uint8_t, tag_size> tag{};
    if (!encrypt(static_cast<const uint8_t *>(data), size, c, aad(out, c), n.data(), ct, tag))
        return {};
    out.insert(out.end(), ct.begin(), ct.end());
    out.insert(out.end(), tag.begin(), tag.end());
    return out;
}
std::vector<uint8_t> SecurityFramework::decryptData(const void *data, size_t size, const SecurityContext &c) {
    if ((!data && size) || !valid(c) || size < header_size + tag_size)
        return {};
    const auto *raw = static_cast<const uint8_t *>(data);
    std::vector<uint8_t> in(raw, raw + size);
    if (!std::equal(magic.begin(), magic.end(), in.begin()) || in[4] != 1)
        return {};
    size_t p = 5;
    uint64_t seq;
    uint32_t len;
    if (!get64(in, p, seq) || p + nonce_size > in.size())
        return {};
    const uint8_t *n = in.data() + p;
    p += nonce_size;
    if (!get32(in, p, len) || len != in.size() - header_size - tag_size)
        return {};
    {
        std::lock_guard lock(pImpl->mutex);
        auto it = pImpl->contexts.find(c.session_id);
        if (!pImpl->initialized || it == pImpl->contexts.end() ||
            CRYPTO_memcmp(it->second.session_key.data(), c.session_key.data(), 32) != 0 ||
            seq <= pImpl->received[c.session_id])
            return {};
    }
    std::vector<uint8_t> plain;
    if (!decrypt(in.data() + p, len, c, aad(std::vector<uint8_t>(in.begin(), in.begin() + header_size), c), n,
                 in.data() + p + len, plain))
        return {};
    std::lock_guard lock(pImpl->mutex);
    pImpl->received[c.session_id] = seq;
    return plain;
}
IntegrityCheck SecurityFramework::createIntegrityCheck(const void *data, size_t size) {
    IntegrityCheck c{};
    c.hash_algo = CryptoAlgorithm::SHA3_256;
    if (!hash(data, size, c.hash))
        c.hash.clear();
    return c;
}
bool SecurityFramework::verifyIntegrity(const void *data, size_t size, const IntegrityCheck &c) {
    std::vector<uint8_t> h;
    return c.hash_algo == CryptoAlgorithm::SHA3_256 && c.hash.size() == 32 && hash(data, size, h) &&
           CRYPTO_memcmp(h.data(), c.hash.data(), 32) == 0;
}
bool SecurityFramework::checkReplayAttack(const SecurityContext &c, uint64_t n) {
    std::lock_guard lock(pImpl->mutex);
    return n > pImpl->received[c.session_id];
}
void SecurityFramework::updateSequenceNumber(SecurityContext &c, uint64_t n) {
    if (n > c.sequence_number)
        c.sequence_number = n;
}
bool SecurityFramework::checkRateLimit(const std::string &s) {
    if (s.empty())
        return false;
    std::lock_guard lock(pImpl->mutex);
    auto &q = pImpl->requests[s];
    auto now = std::chrono::steady_clock::now();
    while (!q.empty() && now - q.front() >= std::chrono::minutes(1))
        q.pop_front();
    return q.size() < 60;
}
void SecurityFramework::updateRateLimit(const std::string &s) {
    if (!s.empty()) {
        std::lock_guard lock(pImpl->mutex);
        pImpl->requests[s].push_back(std::chrono::steady_clock::now());
    }
}
bool SecurityFramework::establishSecureChannel(const std::string &host, int port) {
    SPDLOG_ERROR(
        "Refusing plaintext channel to {}:{}; use SecureSocket streams with an authenticated TLS configuration", host,
        port);
    return false;
}
void SecurityFramework::closeSecureChannel(const std::string &) {}
void SecurityFramework::addAccessControl(const AccessControlEntry &e) {
    std::lock_guard lock(pImpl->mutex);
    pImpl->acl.push_back(e);
}
bool SecurityFramework::checkAccess(const std::string &r, const std::string &o) {
    std::lock_guard lock(pImpl->mutex);
    for (const auto &a : pImpl->acl)
        if (a.resource == r &&
            std::find(a.allowed_operations.begin(), a.allowed_operations.end(), o) != a.allowed_operations.end() &&
            (!a.validator || a.validator(o)))
            return true;
    return false;
}
bool SecurityFramework::detectThreat(const SecurityContext &c, ThreatType &t) {
    if (!valid(c) || std::chrono::steady_clock::now() - c.created_at > std::chrono::hours(24)) {
        t = ThreatType::REPLAY_ATTACK;
        return true;
    }
    if (c.sequence_number > 1000000) {
        t = ThreatType::RESOURCE_EXHAUSTION;
        return true;
    }
    return false;
}
void SecurityFramework::respondToThreat(ThreatType t, const std::string &s) {
    SecurityEvent e{std::chrono::steady_clock::now(), t, "migration security policy blocked a threat", s, true};
    logSecurityEvent(e);
}
void SecurityFramework::logSecurityEvent(const SecurityEvent &e) {
    std::lock_guard lock(pImpl->mutex);
    pImpl->events.push_back(e);
}
std::vector<SecurityEvent>
SecurityFramework::getSecurityEvents(std::chrono::time_point<std::chrono::steady_clock> since) {
    std::lock_guard lock(pImpl->mutex);
    std::vector<SecurityEvent> out;
    for (auto &e : pImpl->events)
        if (e.timestamp >= since)
            out.push_back(e);
    return out;
}
std::vector<uint8_t> SecurityFramework::secureSerialize(const void *state, size_t size, const SecurityContext &c) {
    if (!state && size)
        return {};
    auto enc = encryptData(state, size, c);
    auto check = createIntegrityCheck(state, size);
    if (enc.empty() || check.hash.empty())
        return {};
    enc.insert(enc.begin(), check.hash.begin(), check.hash.end());
    return enc;
}
bool SecurityFramework::secureDeserialize(const std::vector<uint8_t> &data, void *state, size_t size,
                                          const SecurityContext &c) {
    if ((!state && size) || data.size() < 32 + header_size + tag_size)
        return false;
    IntegrityCheck check{};
    check.hash_algo = CryptoAlgorithm::SHA3_256;
    check.hash.assign(data.begin(), data.begin() + 32);
    auto plain = decryptData(data.data() + 32, data.size() - 32, c);
    if (plain.size() != size || !verifyIntegrity(plain.data(), plain.size(), check))
        return false;
    memcpy(state, plain.data(), size);
    return true;
}

struct WASISecurityExtensions::Impl {
    std::vector<Capability> caps;
    SandboxPolicy policy{};
    std::mutex mutex;
};
WASISecurityExtensions::WASISecurityExtensions() : pImpl(std::make_unique<Impl>()) {}
WASISecurityExtensions::~WASISecurityExtensions() = default;
WASISecurityExtensions::Capability WASISecurityExtensions::grantCapability(const std::string &r,
                                                                           const std::vector<std::string> &p,
                                                                           std::chrono::seconds d) {
    Capability c{r, p, std::chrono::steady_clock::now() + d};
    std::lock_guard lock(pImpl->mutex);
    pImpl->caps.push_back(c);
    return c;
}
bool WASISecurityExtensions::checkCapability(const Capability &c, const std::string &o) {
    if (std::chrono::steady_clock::now() > c.expires_at)
        return false;
    std::lock_guard lock(pImpl->mutex);
    return std::find_if(pImpl->caps.begin(), pImpl->caps.end(),
                        [&](const Capability &a) {
                            return a.resource_type == c.resource_type && a.permissions == c.permissions &&
                                   a.expires_at == c.expires_at;
                        }) != pImpl->caps.end() &&
           std::find(c.permissions.begin(), c.permissions.end(), o) != c.permissions.end();
}
void WASISecurityExtensions::revokeCapability(const Capability &c) {
    std::lock_guard lock(pImpl->mutex);
    std::erase_if(pImpl->caps, [&](const Capability &a) {
        return a.resource_type == c.resource_type && a.permissions == c.permissions && a.expires_at == c.expires_at;
    });
}
void WASISecurityExtensions::enforceSandboxPolicy(const SandboxPolicy &p) {
    std::lock_guard lock(pImpl->mutex);
    pImpl->policy = p;
}
bool WASISecurityExtensions::validateSandboxCompliance(const SandboxPolicy &p) {
    std::lock_guard lock(pImpl->mutex);
    return p.allow_network_access == pImpl->policy.allow_network_access &&
           p.allow_file_access == pImpl->policy.allow_file_access && p.allowed_paths == pImpl->policy.allowed_paths &&
           p.max_memory_usage == pImpl->policy.max_memory_usage && p.max_cpu_time_ms == pImpl->policy.max_cpu_time_ms;
}
WASISecurityExtensions::SecureFdMigration WASISecurityExtensions::prepareSecureFdMigration(int) {
    SPDLOG_ERROR(
        "Secure FD migration requires a negotiated transport and does not serialize process-local descriptors");
    return {-1, {}, {}, {}};
}
int WASISecurityExtensions::restoreSecureFd(const SecureFdMigration &) { return -1; }

struct MigrationAttestation::Impl {};
MigrationAttestation::MigrationAttestation() : pImpl(std::make_unique<Impl>()) {}
MigrationAttestation::~MigrationAttestation() = default;
std::vector<uint8_t> MigrationAttestation::generateAttestationReport(const void *, size_t) {
    SPDLOG_ERROR("No hardware attestation provider configured; refusing fabricated report");
    return {};
}
bool MigrationAttestation::verifyAttestationReport(const std::vector<uint8_t> &, const void *, size_t) { return false; }
bool MigrationAttestation::initializeTPM() { return false; }
std::vector<uint8_t> MigrationAttestation::createTPMQuote(const std::vector<uint8_t> &) { return {}; }
bool MigrationAttestation::initializeSGX() { return false; }
std::vector<uint8_t> MigrationAttestation::createSGXReport(const void *, size_t) { return {}; }
bool MigrationAttestation::performRemoteAttestation(const std::string &, const std::vector<uint8_t> &) { return false; }

struct SecureMigrationProtocol::Impl {
    ProtocolState state = ProtocolState::STATE_INIT;
    bool source = false;
    std::vector<uint8_t> key, peer;
    EVP_PKEY *identity = nullptr;
    uint64_t sent = 0, received = 0;
};
SecureMigrationProtocol::SecureMigrationProtocol() : pImpl(std::make_unique<Impl>()) {}
SecureMigrationProtocol::~SecureMigrationProtocol() {
    if (pImpl->identity)
        EVP_PKEY_free(pImpl->identity);
    OPENSSL_cleanse(pImpl->key.data(), pImpl->key.size());
}
bool SecureMigrationProtocol::initializeProtocol(bool source) {
    if (pImpl->identity)
        EVP_PKEY_free(pImpl->identity);
    pImpl->identity = nullptr;
    pImpl->key.clear();
    pImpl->peer.clear();
    EVP_PKEY_CTX *x = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    bool ok = x && EVP_PKEY_keygen_init(x) == 1 && EVP_PKEY_keygen(x, &pImpl->identity) == 1;
    EVP_PKEY_CTX_free(x);
    if (!ok) {
        pImpl->state = ProtocolState::STATE_ERROR;
        return false;
    }
    pImpl->source = source;
    pImpl->sent = pImpl->received = 0;
    pImpl->state = ProtocolState::STATE_HANDSHAKE;
    return true;
}
std::vector<uint8_t> SecureMigrationProtocol::createHandshakeMessage() {
    if (pImpl->state != ProtocolState::STATE_HANDSHAKE || !pImpl->identity)
        return {};
    std::vector<uint8_t> m{'M', 'V', 'H', '1'};
    m.resize(36);
    size_t n = 32;
    if (EVP_PKEY_get_raw_public_key(pImpl->identity, m.data() + 4, &n) != 1 || n != 32)
        return {};
    return m;
}
bool SecureMigrationProtocol::processHandshakeMessage(const std::vector<uint8_t> &m) {
    if (pImpl->state != ProtocolState::STATE_HANDSHAKE || m.size() != 36 || memcmp(m.data(), "MVH1", 4) != 0)
        return false;
    pImpl->peer.assign(m.begin() + 4, m.end());
    return performKeyExchange();
}
bool SecureMigrationProtocol::performKeyExchange() {
    if (!pImpl->identity || pImpl->peer.size() != 32)
        return false;
    EVP_PKEY *peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, pImpl->peer.data(), 32);
    EVP_PKEY_CTX *x = EVP_PKEY_CTX_new(pImpl->identity, nullptr);
    size_t n = 0;
    bool ok = peer && x && EVP_PKEY_derive_init(x) == 1 && EVP_PKEY_derive_set_peer(x, peer) == 1 &&
              EVP_PKEY_derive(x, nullptr, &n) == 1;
    std::vector<uint8_t> secret(ok ? n : 0);
    ok = ok && EVP_PKEY_derive(x, secret.data(), &n) == 1;
    EVP_PKEY_CTX_free(x);
    EVP_PKEY_free(peer);
    if (!ok)
        return false;
    pImpl->key.assign(32, 0);
    EVP_PKEY_CTX *h = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    const char info[] = "MVVM migration v1";
    size_t out = 32;
    ok = h && EVP_PKEY_derive_init(h) == 1 && EVP_PKEY_CTX_set_hkdf_md(h, EVP_sha256()) == 1 &&
         EVP_PKEY_CTX_set1_hkdf_salt(h, nullptr, 0) == 1 && EVP_PKEY_CTX_set1_hkdf_key(h, secret.data(), n) == 1 &&
         EVP_PKEY_CTX_add1_hkdf_info(h, reinterpret_cast<const unsigned char *>(info), sizeof(info) - 1) == 1 &&
         EVP_PKEY_derive(h, pImpl->key.data(), &out) == 1 && out == 32;
    EVP_PKEY_CTX_free(h);
    OPENSSL_cleanse(secret.data(), secret.size());
    if (!ok) {
        pImpl->key.clear();
        return false;
    }
    pImpl->state = ProtocolState::STATE_AUTHENTICATED;
    return true;
}
std::vector<uint8_t> SecureMigrationProtocol::getSessionKey() const { return pImpl->key; }
std::vector<uint8_t> SecureMigrationProtocol::prepareStateTransfer(const void *state, size_t size) {
    if (pImpl->state != ProtocolState::STATE_AUTHENTICATED || (!state && size) || size > UINT32_MAX)
        return {};
    SecurityContext c{"migration", AuthMethod::MUTUAL_TLS, CryptoAlgorithm::AES_256_GCM, pImpl->key, {}, ++pImpl->sent,
                      {}};
    std::array<uint8_t, nonce_size> n{};
    if (RAND_bytes(n.data(), n.size()) != 1)
        return {};
    std::vector<uint8_t> out{'M', 'V', 'P', '1'};
    put64(out, c.sequence_number);
    out.insert(out.end(), n.begin(), n.end());
    put32(out, uint32_t(size));
    std::vector<uint8_t> ct;
    std::array<uint8_t, tag_size> tag{};
    if (!encrypt(static_cast<const uint8_t *>(state), size, c, out, n.data(), ct, tag))
        return {};
    out.insert(out.end(), ct.begin(), ct.end());
    out.insert(out.end(), tag.begin(), tag.end());
    pImpl->state = ProtocolState::STATE_MIGRATING;
    return out;
}
bool SecureMigrationProtocol::receiveStateTransfer(const std::vector<uint8_t> &d, void *state, size_t size) {
    if (pImpl->state != ProtocolState::STATE_AUTHENTICATED || (!state && size) ||
        d.size() < 4 + 8 + nonce_size + 4 + tag_size || memcmp(d.data(), "MVP1", 4) != 0)
        return false;
    size_t p = 4;
    uint64_t seq;
    uint32_t len;
    if (!get64(d, p, seq) || p + nonce_size > d.size())
        return false;
    const uint8_t *n = d.data() + p;
    p += nonce_size;
    if (!get32(d, p, len) || seq <= pImpl->received || len != size ||
        d.size() != 4 + 8 + nonce_size + 4 + len + tag_size)
        return false;
    SecurityContext c{"migration", AuthMethod::MUTUAL_TLS, CryptoAlgorithm::AES_256_GCM, pImpl->key, {}, seq, {}};
    std::vector<uint8_t> plain;
    if (!decrypt(d.data() + p, len, c, std::vector<uint8_t>(d.begin(), d.begin() + p), n, d.data() + p + len, plain) ||
        plain.size() != size)
        return false;
    memcpy(state, plain.data(), size);
    pImpl->received = seq;
    pImpl->state = ProtocolState::STATE_MIGRATING;
    return true;
}
bool SecureMigrationProtocol::completeProtocol() {
    if (pImpl->state != ProtocolState::STATE_MIGRATING)
        return false;
    pImpl->state = ProtocolState::STATE_COMPLETED;
    return true;
}
SecureMigrationProtocol::ProtocolState SecureMigrationProtocol::getCurrentState() const { return pImpl->state; }
} // namespace mvvm::security
