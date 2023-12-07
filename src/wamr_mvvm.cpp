#include "wamr.h"

bool WAMRInstance::get_int3_addr() {
    if (!is_aot)
        return true;
    auto module = get_module();
    auto code = static_cast<unsigned char *>(module->code);
    auto code_size = module->code_size;
    //fprintf(stderr, "code %p code_size %d\n", code, code_size);

    std::string object_file = std::string(aot_file_path) + ".o";
    // if not exist, exit
#if defined(_WIN32)
    auto stringToWChar = [](const std::string& s) -> wchar_t* {
        int len;
        int slength = static_cast<int>(s.length()) + 1;
        len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
        wchar_t* buf = new wchar_t[len];
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
    FILE *fp = _popen(("objdump -d " + object_file + " | grep -E int3$").c_str(), "r");
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
        output += buf;
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
            addr.push_back(line.substr(0, pos));
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

bool WAMRInstance::replace_int3_with_nop() {
    if (!is_aot)
        return true;
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
#ifdef __x86_64__
        code[offset] = 0x90;
#elif __aarch64__
        code[offset + 3] = 0xd5;
        code[offset + 2] = 0x03;
        code[offset + 1] = 0x20;
        code[offset] = 0x1f;
#endif
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

bool WAMRInstance::replace_mfence_with_nop() {
    if (!is_aot)
        return true;
    auto module = get_module();
    auto code = static_cast<unsigned char *>(module->code);
    auto code_size = module->code_size;

    // LOGV_DEBUG << "Making the code section writable";
    {
        int map_prot = MMAP_PROT_READ | MMAP_PROT_WRITE;

        uint8 *mmap_addr = module->literal - sizeof(uint32);
        uint32 total_size = sizeof(uint32) + module->literal_size + module->code_size;
        os_mprotect(mmap_addr, total_size, map_prot);
    }

    // replace int3 with nop
    for (auto offset : int3_addr) {
        if (code[offset-3] == 0x0f && code[offset-2] == 0xae && code[offset-1] == 0xf0) {
            code[offset-3] = 0x90;
            code[offset-2] = 0x90;
            code[offset-1] = 0x90;
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

bool WAMRInstance::replace_nop_with_int3() {
    if (!is_aot)
        return true;
    auto module = get_module();
    auto code = static_cast<unsigned char *>(module->code);
    auto code_size = module->code_size;

    // LOGV_DEBUG << "Making the code section writable";
    {
        int map_prot = MMAP_PROT_READ | MMAP_PROT_WRITE;

        uint8 *mmap_addr = module->literal - sizeof(uint32);
        uint32 total_size = sizeof(uint32) + module->literal_size + module->code_size;
        os_mprotect(mmap_addr, total_size, map_prot);
    }

    // replace int3 with nop
    for (auto offset : int3_addr) {
        code[offset] = 0xcc;
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
