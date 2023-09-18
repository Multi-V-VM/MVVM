#include "bh_log.h"
#include "wamr.h"

bool WAMRInstance::load_mvvm_aot_metadata(const char *file_path) {
    LOGV_DEBUG << "Loading aot metadata";

    bool result = true;
    std::ifstream fin(file_path);

    ArchType arch = ArchType::x86_64;

    enum {
        End, // eof
        Top, // arch, nop
        NopNumber, // number of nops
        NopAddress, // addresses of nops
    } state = Top;

    while (state != End) {
        switch (state) {
        case End:
            break;
        case Top: {
            std::string s;
            fin >> s;
            if (s == "x86_64") {
                arch = ArchType::x86_64;
                mvvm_aot_metadatas[arch] = MVVMAotMetadata();
                state = Top;
            } else if (s == "nop") {
                state = NopNumber;
            } else {
                state = End;
            }
            break;
        }
        case NopNumber: {
            std::size_t n;
            fin >> n;
            mvvm_aot_metadatas.at(arch).nops.resize(n);
            state = NopAddress;
            break;
        }
        case NopAddress: {
            for (auto &e : mvvm_aot_metadatas.at(arch).nops) {
                fin >> std::hex >> e;
            }
            state = Top;
            break;
        }
        }
    }

    for (auto &[arch, aot_metadata] : mvvm_aot_metadatas) {
        switch (arch) {
        case ArchType::x86_64: {
            LOGV_DEBUG << "x86_64";
            break;
        }
        }
        LOGV_DEBUG << "number of nops: " << aot_metadata.nops.size();
        LOGV_DEBUG << "-------";
    }

    return result;
}