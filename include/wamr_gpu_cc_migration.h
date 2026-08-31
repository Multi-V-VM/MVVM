#ifndef WAMR_GPU_CC_MIGRATION_H
#define WAMR_GPU_CC_MIGRATION_H

#include "wamr_gpu_cc_framework.h"
#include "wamr_read_write.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string>
#include <vector>

namespace mvvm::gpu::migration {

constexpr size_t key_size = 32;
constexpr size_t digest_size = 64;
constexpr size_t nonce_size = 12;
constexpr size_t tag_size = 16;
constexpr uint64_t max_snapshot_size = 1ULL << 34; // 16 GiB, also bounds hostile input.
constexpr uint32_t max_report_size = 1U << 20;

struct Allocation {
    uint64_t old_address = 0;
    std::vector<uint8_t> contents;
};

struct State {
    std::string device_name;
    std::vector<GPUKernel> kernels;
    std::vector<Allocation> allocations;
};

class Encoder {
public:
    std::vector<uint8_t> data;

    void u32(uint32_t value) {
        for (unsigned shift = 0; shift != 32; shift += 8)
            data.push_back(static_cast<uint8_t>(value >> shift));
    }
    void u64(uint64_t value) {
        for (unsigned shift = 0; shift != 64; shift += 8)
            data.push_back(static_cast<uint8_t>(value >> shift));
    }
    bool bytes(const void *value, size_t size) {
        if ((!value && size) || size > max_snapshot_size || data.size() > max_snapshot_size - size)
            return false;
        if (size == 0)
            return true;
        const auto *p = static_cast<const uint8_t *>(value);
        data.insert(data.end(), p, p + size);
        return true;
    }
    bool sized(const void *value, size_t size) {
        if (size > UINT32_MAX)
            return false;
        u32(static_cast<uint32_t>(size));
        return bytes(value, size);
    }
    bool string(const std::string &value) { return sized(value.data(), value.size()); }
};

class Decoder {
public:
    explicit Decoder(const std::vector<uint8_t> &input) : data(input) {}

    bool u32(uint32_t &value) {
        if (position > data.size() || data.size() - position < 4)
            return false;
        value = 0;
        for (unsigned shift = 0; shift != 32; shift += 8)
            value |= uint32_t(data[position++]) << shift;
        return true;
    }
    bool u64(uint64_t &value) {
        if (position > data.size() || data.size() - position < 8)
            return false;
        value = 0;
        for (unsigned shift = 0; shift != 64; shift += 8)
            value |= uint64_t(data[position++]) << shift;
        return true;
    }
    bool bytes(void *value, size_t size) {
        if ((!value && size) || position > data.size() || size > data.size() - position)
            return false;
        std::memcpy(value, data.data() + position, size);
        position += size;
        return true;
    }
    bool sized(std::vector<uint8_t> &value) {
        uint32_t size = 0;
        if (!u32(size) || position > data.size() || size > data.size() - position)
            return false;
        value.assign(data.begin() + position, data.begin() + position + size);
        position += size;
        return true;
    }
    bool string(std::string &value) {
        std::vector<uint8_t> bytes_value;
        if (!sized(bytes_value))
            return false;
        value.assign(bytes_value.begin(), bytes_value.end());
        return true;
    }
    bool done() const { return position == data.size(); }

private:
    const std::vector<uint8_t> &data;
    size_t position = 0;
};

inline bool encodeState(const State &state, std::vector<uint8_t> &plain) {
    if (state.kernels.size() > UINT32_MAX || state.allocations.size() > UINT32_MAX)
        return false;
    Encoder out;
    if (!out.string(state.device_name))
        return false;
    out.u32(static_cast<uint32_t>(state.kernels.size()));
    for (const auto &kernel : state.kernels) {
        if (!out.string(kernel.name) || !out.sized(kernel.binary_code.data(), kernel.binary_code.size()))
            return false;
        out.u64(kernel.num_parameters);
        out.u64(kernel.required_shared_memory);
        if (!out.sized(kernel.signature.data(), kernel.signature.size()))
            return false;
    }
    out.u32(static_cast<uint32_t>(state.allocations.size()));
    for (const auto &allocation : state.allocations) {
        out.u64(allocation.old_address);
        if (!out.sized(allocation.contents.data(), allocation.contents.size()))
            return false;
    }
    plain = std::move(out.data);
    return true;
}

inline bool decodeState(const std::vector<uint8_t> &plain, State &state) {
    Decoder in(plain);
    uint32_t count = 0;
    State decoded;
    if (!in.string(decoded.device_name) || !in.u32(count) || count > 1'000'000)
        return false;
    decoded.kernels.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        GPUKernel kernel{};
        uint64_t parameters = 0, shared = 0;
        if (!in.string(kernel.name) || !in.sized(kernel.binary_code) || !in.u64(parameters) || !in.u64(shared) ||
            !in.sized(kernel.signature) || parameters > SIZE_MAX || shared > SIZE_MAX)
            return false;
        kernel.num_parameters = static_cast<size_t>(parameters);
        kernel.required_shared_memory = static_cast<size_t>(shared);
        decoded.kernels.push_back(std::move(kernel));
    }
    if (!in.u32(count) || count > 1'000'000)
        return false;
    decoded.allocations.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        Allocation allocation;
        if (!in.u64(allocation.old_address) || !in.sized(allocation.contents))
            return false;
        decoded.allocations.push_back(std::move(allocation));
    }
    if (!in.done())
        return false;
    state = std::move(decoded);
    return true;
}

inline bool sha512(const std::vector<uint8_t> &input, std::array<uint8_t, digest_size> &digest) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned int size = 0;
    const bool ok = ctx && EVP_DigestInit_ex(ctx, EVP_sha512(), nullptr) == 1 &&
                    EVP_DigestUpdate(ctx, input.data(), input.size()) == 1 &&
                    EVP_DigestFinal_ex(ctx, digest.data(), &size) == 1 && size == digest.size();
    EVP_MD_CTX_free(ctx);
    return ok;
}

inline std::array<uint8_t, 16> aad(GPUVendor vendor) {
    std::array<uint8_t, 16> value{{'M', 'V', 'V', 'M', 'G', 'C', 'C', '2', 2, 0, 0, 0, 0, 0, 0, 0}};
    const uint32_t v = static_cast<uint32_t>(vendor);
    for (unsigned i = 0; i != 4; ++i)
        value[12 + i] = static_cast<uint8_t>(v >> (8 * i));
    return value;
}

inline bool encrypt(const std::vector<uint8_t> &plain, const std::array<uint8_t, key_size> &key, GPUVendor vendor,
                    std::array<uint8_t, nonce_size> &nonce, std::array<uint8_t, tag_size> &tag,
                    std::vector<uint8_t> &ciphertext) {
    if (plain.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        RAND_bytes(nonce.data(), nonce.size()) != 1)
        return false;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    ciphertext.assign(plain.size(), 0);
    int n = 0, tail = 0;
    const auto associated = aad(vendor);
    const bool ok = ctx && EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
                    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) == 1 &&
                    EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) == 1 &&
                    EVP_EncryptUpdate(ctx, nullptr, &n, associated.data(), associated.size()) == 1 &&
                    EVP_EncryptUpdate(ctx, ciphertext.data(), &n, plain.data(), plain.size()) == 1 &&
                    EVP_EncryptFinal_ex(ctx, ciphertext.data() + n, &tail) == 1 &&
                    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tag.size(), tag.data()) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        ciphertext.clear();
        return false;
    }
    ciphertext.resize(n + tail);
    return ciphertext.size() == plain.size();
}

inline bool decrypt(const std::vector<uint8_t> &ciphertext, const std::array<uint8_t, key_size> &key,
                    GPUVendor vendor, const std::array<uint8_t, nonce_size> &nonce,
                    const std::array<uint8_t, tag_size> &tag, std::vector<uint8_t> &plain) {
    if (ciphertext.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        return false;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    plain.assign(ciphertext.size(), 0);
    int n = 0, tail = 0;
    const auto associated = aad(vendor);
    const bool ok = ctx && EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
                    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) == 1 &&
                    EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) == 1 &&
                    EVP_DecryptUpdate(ctx, nullptr, &n, associated.data(), associated.size()) == 1 &&
                    EVP_DecryptUpdate(ctx, plain.data(), &n, ciphertext.data(), ciphertext.size()) == 1 &&
                    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag.size(), const_cast<uint8_t *>(tag.data())) == 1 &&
                    EVP_DecryptFinal_ex(ctx, plain.data() + n, &tail) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        if (!plain.empty())
            OPENSSL_cleanse(plain.data(), plain.size());
        plain.clear();
        return false;
    }
    plain.resize(n + tail);
    return plain.size() == ciphertext.size();
}

using Attest = bool (*)(const std::vector<uint8_t> &, std::vector<uint8_t> &);
using CheckBinding = bool (*)(const std::vector<uint8_t> &, const std::array<uint8_t, digest_size> &);

inline bool write(WriteStream *writer, GPUVendor vendor, const State &state,
                  const std::array<uint8_t, key_size> &key, Attest attest) {
    if (!writer || !attest)
        return false;
    std::vector<uint8_t> plain, ciphertext, report;
    std::array<uint8_t, nonce_size> nonce{};
    std::array<uint8_t, tag_size> tag{};
    std::array<uint8_t, digest_size> digest{};
    if (!encodeState(state, plain) || !encrypt(plain, key, vendor, nonce, tag, ciphertext) ||
        !sha512(ciphertext, digest) ||
        !attest(std::vector<uint8_t>(digest.begin(), digest.end()), report) || report.empty() ||
        report.size() > max_report_size) {
        if (!plain.empty())
            OPENSSL_cleanse(plain.data(), plain.size());
        return false;
    }
    Encoder header;
    const auto associated = aad(vendor);
    header.bytes(associated.data(), associated.size());
    header.u32(static_cast<uint32_t>(report.size()));
    header.u64(ciphertext.size());
    header.bytes(nonce.data(), nonce.size());
    header.bytes(tag.data(), tag.size());
    header.bytes(digest.data(), digest.size());
    const bool ok = writer->write(reinterpret_cast<const char *>(header.data.data()), header.data.size()) &&
                    writer->write(reinterpret_cast<const char *>(report.data()), report.size()) &&
                    writer->write(reinterpret_cast<const char *>(ciphertext.data()), ciphertext.size());
    if (!plain.empty())
        OPENSSL_cleanse(plain.data(), plain.size());
    return ok;
}

inline bool read(ReadStream *reader, GPUVendor vendor, const std::array<uint8_t, key_size> &key,
                 CheckBinding check_binding, State &state, std::vector<uint8_t> &source_report,
                 std::array<uint8_t, digest_size> &state_digest) {
    constexpr size_t header_size = 16 + 4 + 8 + nonce_size + tag_size + digest_size;
    if (!reader || !check_binding)
        return false;
    std::vector<uint8_t> header(header_size);
    if (!reader->read(reinterpret_cast<char *>(header.data()), header.size()))
        return false;
    Decoder in(header);
    std::array<uint8_t, 16> associated{};
    std::array<uint8_t, nonce_size> nonce{};
    std::array<uint8_t, tag_size> tag{};
    std::array<uint8_t, digest_size> expected{}, actual{};
    uint32_t report_size = 0;
    uint64_t ciphertext_size = 0;
    if (!in.bytes(associated.data(), associated.size()) || associated != aad(vendor) || !in.u32(report_size) ||
        !in.u64(ciphertext_size) || !in.bytes(nonce.data(), nonce.size()) || !in.bytes(tag.data(), tag.size()) ||
        !in.bytes(expected.data(), expected.size()) || !in.done() || report_size == 0 ||
        report_size > max_report_size || ciphertext_size > max_snapshot_size || ciphertext_size > SIZE_MAX)
        return false;
    source_report.resize(report_size);
    std::vector<uint8_t> ciphertext(static_cast<size_t>(ciphertext_size)), plain;
    if (!reader->read(reinterpret_cast<char *>(source_report.data()), source_report.size()) ||
        !reader->read(reinterpret_cast<char *>(ciphertext.data()), ciphertext.size()) || !sha512(ciphertext, actual) ||
        CRYPTO_memcmp(actual.data(), expected.data(), expected.size()) != 0 ||
        !check_binding(source_report, expected) || !decrypt(ciphertext, key, vendor, nonce, tag, plain))
        return false;
    const bool ok = decodeState(plain, state);
    if (ok)
        state_digest = expected;
    if (!plain.empty())
        OPENSSL_cleanse(plain.data(), plain.size());
    return ok;
}

} // namespace mvvm::gpu::migration

#endif
