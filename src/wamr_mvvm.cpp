/*
 * The WebAssembly Live Migration Project
 *
 *  By: Aibo Hu
 *      Yiwei Yang
 *      Brian Zhao
 *      Andrew Quinn
 *
 *  Copyright 2024 Regents of the Univeristy of California
 *  UC Santa Cruz Sluglab.
 */

#include "wamr.h"
#if defined(_WIN32)
#include <detours/detours.h>
#include <windows.h>
#endif

bool WAMRInstance::get_int3_addr() {
    if (!is_aot)
        return true;
    auto m_ = get_module();
    auto code = static_cast<unsigned char *>(m_->code);
    auto code_size = m_->code_size;
    fprintf(stderr, "code %p code_size %d\n", code, code_size);

    std::string object_file = std::string(aot_file_path) + ".o";
    // if not exist, exit
#if defined(_WIN32)
    auto stringToWChar = [](const std::string &s) -> wchar_t * {
        int len;
        int slength = static_cast<int>(s.length()) + 1;
        len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
        wchar_t *buf = new wchar_t[len];
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
        return buf;
    };

    if (_waccess(stringToWChar(object_file), F_OK) == -1) {
#else
    if (access(object_file.c_str(), F_OK) == -1) {
#endif
        fprintf(stderr, "object file %s not exist\n", object_file.c_str());
        return false;
    }

    // disassemble object file and get the output
#ifdef __x86_64__
    auto test_cmd = "objdump -d " + object_file + " | grep -E int3$";
#elif __aarch64__
    std::string test_cmd = "objdump -d " + object_file + " | grep -E svc";
#endif
#if defined(_WIN32)
    FILE *fp = _popen(("llvm-objdump -d " + object_file + " | grep -E \"ba 04 00 00 00\"").c_str(), "r");
#else
    FILE *fp = popen(test_cmd.c_str(), "r");
#endif
    if (fp == nullptr) {
        fprintf(stderr, "popen failed\n");
        return false;
    }
    char buf[1024];
    std::string output;
    while (fgets(buf, sizeof(buf), fp) != nullptr) {
#if defined(_WIN32)
        output += std::string(buf);
#else
        output += std::string(buf);
#endif
    }
#if defined(_WIN32)
    _pclose(fp);
#else
    pclose(fp);
#endif

    // split the output
    std::vector<std::string> lines;
    std::string line;
    std::istringstream iss(output);
    while (std::getline(iss, line)) {
        lines.push_back(line);
    }

    // get the address of int3
    std::vector<std::string> addr;
    for (auto &line : lines) {
        auto pos = line.find(":");
        if (pos != std::string::npos) {
            addr.emplace_back(line.substr(0, pos));
        }
    }

    for (auto &a : addr) {
        auto addr = a;
        auto offset = std::stoul(addr, nullptr, 16);
#ifdef __x86_64__
        if (code[offset] != 0xcc) {
            fprintf(stderr, "code[%lu] != 0xcc\n", offset);
            return false;
        }
#elif __aarch64__
        if (code[offset + 3] != 0xd4) {
            fprintf(stderr, "code[%lu] != 0xd4\n", offset);
            return false;
        }
#endif
        if (offset < code_size) {
            int3_addr.push_back(offset);
        }
    }
    return true;
}
#if defined(_WIN32)
extern "C" int raise(int sig);

// Pointer to the original 'raise' function
static int(WINAPI *TrueRaise)(int sig) = raise;

// Our replacement function
inline int WINAPI MyRaise(int sig) { return 0; }
#endif

bool WAMRInstance::replace_int3_with_nop() {
    if (!is_aot)
        return true;
#if defined(_WIN32)
    DetourRestoreAfterWith();
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID &)TrueRaise, MyRaise);

    if (DetourTransactionCommit() == NO_ERROR) {
        SPDLOG_DEBUG("Successfully detoured raise.\n");
    } else {
        SPDLOG_DEBUG("Failed to detour raise.\n");
        return false;
    }
#else
    auto module = get_module();
    auto code = static_cast<unsigned char *>(module->code);
    auto code_size = module->code_size;

    // LOGV_DEBUG << "Making the code section writable";
    {
#if defined(__APPLE__)
        pthread_jit_write_protect_np(0);
#endif
        int map_prot = MMAP_PROT_READ | MMAP_PROT_WRITE;

        uint8 *mmap_addr = module->literal - sizeof(uint32);
        uint32 total_size = sizeof(uint32) + module->literal_size + module->code_size;
        os_mprotect(mmap_addr, total_size, map_prot);
    }

    // replace int3 with nop
    for (auto offset : int3_addr) {
#if defined(_WIN32)
        printf("%lld  ", offset);
        for (int i = 0; i < 5; i++) {
            printf("%02x ", code[offset + i]);
        }
#else
#ifdef __x86_64__
        code[offset] = 0x90;
#elif __aarch64__
        code[offset + 3] = 0xd5;
        code[offset + 2] = 0x03;
        code[offset + 1] = 0x20;
        code[offset] = 0x1f;
#endif
#endif
    }

    SPDLOG_DEBUG("Making the code section executable");
    {
        int map_prot = MMAP_PROT_READ | MMAP_PROT_EXEC;

        uint8 *mmap_addr = module->literal - sizeof(uint32);
        uint32 total_size = sizeof(uint32) + module->literal_size + module->code_size;
        os_mprotect(mmap_addr, total_size, map_prot);
    }
#if defined(__APPLE__)
    pthread_jit_write_protect_np(1);
#endif
#endif
    return true;
}

bool WAMRInstance::replace_mfence_with_nop() {
    if (!is_aot)
        return true;
    auto module = get_module();
    auto code = static_cast<unsigned char *>(module->code);
    auto code_size = module->code_size;

    SPDLOG_DEBUG("Making the code section writable");
    {
        int map_prot = MMAP_PROT_READ | MMAP_PROT_WRITE;

        uint8 *mmap_addr = module->literal - sizeof(uint32);
        uint32 total_size = sizeof(uint32) + module->literal_size + module->code_size;
        os_mprotect(mmap_addr, total_size, map_prot);
    }

    // replace mfence with nop
    for (auto offset : int3_addr) {
        if (code[offset - 3] == 0x0f && code[offset - 2] == 0xae && code[offset - 1] == 0xf0) {
            code[offset - 3] = 0x90;
            code[offset - 2] = 0x90;
            code[offset - 1] = 0x90;
        }
    }

    // LOGV_DEBUG << "Making the code section executable";
    {
        int map_prot = MMAP_PROT_READ | MMAP_PROT_EXEC;

        uint8 *mmap_addr = module->literal - sizeof(uint32);
        uint32 total_size = sizeof(uint32) + module->literal_size + module->code_size;
        os_mprotect(mmap_addr, total_size, map_prot);
    }
    return true;
}

const auto rdpmc_instructions = []() -> unsigned long {
#if defined(__x86_64__)
    unsigned long a, d, c;
    c = (1UL << 30);
    __asm__ volatile("rdpmc" : "=a"(a), "=d"(d) : "c"(c));
    return (a | (d << 32));
#elif defined(__aarch64__)
#ifdef __APPLE__
    // macOS ARM64 implementation using PMCCNTR_EL0
    uint64_t val;

    // 读取性能监控周期计数器
    asm volatile("mrs %0, PMCCNTR_EL0" : "=r"(val));

    // 如果计数器未启用，尝试启用它
    if (val == 0) {
        // 启用用户态访问PMU
        uint64_t pmuserenr;
        asm volatile("mrs %0, PMUSERENR_EL0" : "=r"(pmuserenr));
        pmuserenr |= 1; // 启用用户模式访问
        asm volatile("msr PMUSERENR_EL0, %0" ::"r"(pmuserenr));

        // 启用性能计数器
        uint64_t pmcr;
        asm volatile("mrs %0, PMCR_EL0" : "=r"(pmcr));
        pmcr |= (1 << 0); // 启用所有计数器
        pmcr |= (1 << 2); // 重置周期计数器
        asm volatile("msr PMCR_EL0, %0" ::"r"(pmcr));

        // 启用周期计数器
        uint64_t pmcntenset;
        asm volatile("mrs %0, PMCNTENSET_EL0" : "=r"(pmcntenset));
        pmcntenset |= (1 << 31); // 启用周期计数器
        asm volatile("msr PMCNTENSET_EL0, %0" ::"r"(pmcntenset));

        // 再次读取计数器
        asm volatile("mrs %0, PMCCNTR_EL0" : "=r"(val));
    }

    return val;
#else
    // 其他 ARM64 平台的实现
    return 0;
#endif
#else
    return 0;
#endif
};

long WAMRInstance::get_inst_diff() {
    int tile = 0;
    int counter = 0;

    msr_val = rdpmc_instructions();
    cha_counts[tile][counter][1] = msr_val;

    auto res = cha_counts[0][0][1] - cha_counts[0][0][0];
    if (cha_counts[0][0][0] != 0) {
        max_diff = std::max(max_diff, res);
    }

    cha_counts[tile][counter][0] = rdpmc_instructions();

    return res;
}

bool WAMRInstance::replace_nop_with_int3() {
#if defined(_WIN32)
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    // Detach the detour, restoring the original function
    DetourDetach(&(PVOID &)TrueRaise, MyRaise);
    if (DetourTransactionCommit() == NO_ERROR) {
        SPDLOG_DEBUG("Successfully detoured raise.\n");
    } else {
        SPDLOG_DEBUG("Failed to detour raise.\n");
        return false;
    }
#else
    if (!is_aot)
        return true;
    auto module = get_module();
    auto code = static_cast<unsigned char *>(module->code);
    auto code_size = module->code_size;
#if defined(__APPLE__)
    pthread_jit_write_protect_np(0);
#endif
    // LOGV_DEBUG << "Making the code section writable";
    {
        int map_prot = MMAP_PROT_READ | MMAP_PROT_WRITE;

        uint8 *mmap_addr = module->literal - sizeof(uint32);
        uint32 total_size = sizeof(uint32) + module->literal_size + module->code_size;
        os_mprotect(mmap_addr, total_size, map_prot);
    }

    // replace int3 with nop
    for (auto offset : int3_addr) {
#ifdef __x86_64__
        code[offset] = 0xcc;
#elif __aarch64__
        code[offset + 3] = 0xd4;
        code[offset + 2] = 0x00;
        code[offset + 1] = 0x00;
        code[offset] = 0x01;
#endif
    }
    // LOGV_DEBUG << "Making the code section executable";
    {
        int map_prot = MMAP_PROT_READ | MMAP_PROT_EXEC;

        uint8 *mmap_addr = module->literal - sizeof(uint32);
        uint32 total_size = sizeof(uint32) + module->literal_size + module->code_size;
        os_mprotect(mmap_addr, total_size, map_prot);
    }
#if defined(__APPLE__)
    pthread_jit_write_protect_np(1);
#endif
    return true;
#endif
}