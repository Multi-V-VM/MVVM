#include "wamr_gpu_cc_framework.h"
#include "wamr_stream.h"
#include <array>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace mvvm::gpu;
using Bytes = std::vector<uint8_t>;
void require(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
Bytes read(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary);
    require(bool(f), "fixture missing");
    return Bytes(std::istreambuf_iterator<char>(f), {});
}
struct Buffer : WriteStream, ReadStream {
    mutable Bytes bytes;
    mutable size_t pos{};
    bool write(const char *p, size_t n) const override {
        bytes.insert(bytes.end(), p, p + n);
        return true;
    }
    bool read(char *p, size_t n) const override {
        if (n > bytes.size() - pos)
            return false;
        memcpy(p, bytes.data() + pos, n);
        pos += n;
        return true;
    }
};
bool attest(const std::string &python, const std::string &helper, const Bytes &report, const Bytes &binding) {
    std::string challenge;
    for (auto b : binding)
        challenge += std::format("{:02x}", b);
    int fds[2];
    require(pipe(fds) == 0, "pipe failed");
    pid_t pid = fork();
    require(pid >= 0, "fork failed");
    if (pid == 0) {
        dup2(fds[0], STDIN_FILENO);
        close(fds[0]);
        close(fds[1]);
        execl(python.c_str(), python.c_str(), helper.c_str(), challenge.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    close(fds[0]);
    size_t at = 0;
    while (at < report.size()) {
        auto n = write(fds[1], report.data() + at, report.size() - at);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        at += n;
    }
    close(fds[1]);
    int status{};
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    for (;;) {
        const auto waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid)
            break;
        if (waited < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (std::chrono::steady_clock::now() > end) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return at == report.size() && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
int main(int argc, char **argv) {
    try {
        signal(SIGPIPE, SIG_IGN);
        require(argc == 4, "usage: nvidia_gpu_cc_test FIXTURE_DIR PYTHON ATTEST_HELPER");
        auto verifier = [&](GPUVendor v, const Bytes &report, const Bytes &binding) {
            return v == GPUVendor::NVIDIA && attest(argv[2], argv[3], report, binding);
        };
        NVIDIAGPUCCImpl gpu;
        auto devices = gpu.enumerateDevices();
        require(!devices.empty() && gpu.selectDevice(0), "NVIDIA device missing");
        require(!gpu.initializeCC({CCFeature::MEMORY_ENCRYPTION}), "missing verifier accepted");
        require(gpu.setMigrationAttestationVerifier(verifier) &&
                    gpu.initializeCC(
                        {CCFeature::MEMORY_ENCRYPTION, CCFeature::REMOTE_ATTESTATION, CCFeature::SECURE_CHANNELS}),
                "production CC initialization failed");
        require(!gpu.getAttestationReport().empty(), "attestation evidence missing");
        auto device = gpu.getCurrentDevice();
        std::cout << std::format("DEVICE {} CC verified\n", device.name);
        require(!gpu.allocateSecureMemory(32, MemoryType::UNIFIED), "unsupported unified memory accepted");
        void *input = gpu.allocateSecureMemory(32, MemoryType::SECURE);
        void *output = gpu.allocateSecureMemory(16, MemoryType::SECURE);
        require(input && output, "allocation failed");
        std::array<uint8_t, 32> zeros{};
        require(!gpu.copyToDevice(input, zeros.data(), 33), "out-of-range copy accepted");
        uint32_t lanes = 4;
        void *args[]{&input, &output, &lanes};
        size_t passed{};
        std::array<uint8_t, 16> last{};
        for (auto ty : {"f32x4", "i32x4"})
            for (auto op : {"add", "sub", "mul"}) {
                std::string name = std::format("{}_{}", ty, op);
                auto wasm = read(std::filesystem::path(argv[1]) / (name + ".wasm"));
                auto cases = read(std::filesystem::path(argv[1]) / (name + ".cases"));
                require(!cases.empty() && cases.size() % 48 == 0, "invalid CPU oracle cases");
                WasmToGPUTranslator t;
                WasmToGPUTranslator::TranslationOptions options;
                options.target_compute_capability =
                    device.capability.major_version * 10 + device.capability.minor_version;
                auto k = t.translateFunction(wasm.data(), wasm.size(), "op", options);
                require(k.valid() && t.verifyTranslation(k, wasm.data(), wasm.size()), "Wasm translation failed");
                k.name = name;
                auto bad = k;
                bad.binary_code.back() ^= 1;
                require(!gpu.loadSecureKernel(bad), "tampered PTX accepted");
                require(gpu.loadSecureKernel(k), "kernel load failed");
                void *overlapping[]{&input, &input, &lanes};
                require(!gpu.executeSecureKernel(name, overlapping, 3, 1, 4), "overlapping buffers accepted");
                require(!gpu.executeSecureKernel(name, args, 2, 1, 4), "wrong ABI count accepted");
                for (size_t pos = 0; pos < cases.size(); pos += 48) {
                    require(gpu.copyToDevice(input, cases.data() + pos, 32), "input copy failed");
                    require(gpu.executeSecureKernel(name, args, 3, 1, 4), "kernel failed");
                    require(gpu.copyFromDevice(last.data(), output, 16), "output copy failed");
                    require(memcmp(last.data(), cases.data() + pos + 32, 16) == 0,
                            "GPU differs from source Wasm CPU result");
                    ++passed;
                }
            }
        Buffer snapshot;
        Bytes key(32, 0x71);
        require(gpu.setMigrationKey(key) && gpu.checkpointGPUState(&snapshot), "checkpoint failed");
        require(!gpu.checkpointGPUState(&snapshot), "consumed key reused");
        require(gpu.copyToDevice(output, zeros.data(), 16), "source mutation failed");
        NVIDIAGPUCCImpl restored;
        require(restored.selectDevice(0) && restored.setMigrationAttestationVerifier(verifier) &&
                    restored.initializeCC({CCFeature::MEMORY_ENCRYPTION}),
                "destination initialize failed");
        require(restored.setMigrationKey(Bytes(32, 0x72)) && !restored.restoreGPUState(&snapshot),
                "wrong migration key accepted");
        snapshot.pos = 0;
        Buffer corrupt;
        corrupt.bytes = snapshot.bytes;
        corrupt.bytes[corrupt.bytes.size() / 2] ^= 1;
        require(restored.setMigrationKey(key) && !restored.restoreGPUState(&corrupt), "corrupt snapshot accepted");
        require(restored.setMigrationKey(key) && restored.restoreGPUState(&snapshot), "snapshot restore failed");
        auto restored_out = restored.remapRestoredPointer(reinterpret_cast<uint64_t>(output));
        auto restored_in = restored.remapRestoredPointer(reinterpret_cast<uint64_t>(input));
        std::array<uint8_t, 16> actual{};
        require(restored_out && restored_in && restored.copyFromDevice(actual.data(), restored_out, 16) &&
                    actual == last,
                "restored allocation mismatch");
        void *restored_args[]{&restored_in, &restored_out, &lanes};
        require(restored.executeSecureKernel("i32x4_mul", restored_args, 3, 1, 4) &&
                    restored.copyFromDevice(actual.data(), restored_out, 16) && actual == last,
                "restored kernel mismatch");
        std::cout << std::format("{{\"cc_on\":true,\"vendor_attestation\":true,\"wasm_cpu_gpu_cases\":{},\"kernels\":6,"
                                 "\"snapshot_bytes\":{},\"restored_execution\":true}}\n",
                                 passed, snapshot.bytes.size());
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
