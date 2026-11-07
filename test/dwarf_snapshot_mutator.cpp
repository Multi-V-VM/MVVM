#include "mvvm/dwarf_resume.hpp"
#include "mvvm/reflection_codec.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc,char** argv) {
    if(argc!=4) return 2;
    std::ifstream in(argv[1],std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(in),{}};
    auto value=mvvm::codec::decode<mvvm::dwarf::NativeContinuation>(std::as_bytes(std::span(bytes)));
    if(!value || value->frames.empty()) return 3;
    if(std::string_view(argv[3])=="pc") {
        bool found=false;
        for(auto& slot:value->frames.front().slots) if(slot.name=="mvvm_pc") {
            std::uint32_t invalid=UINT32_MAX;
            if(slot.bytes.size()!=sizeof(invalid)) return 4;
            std::memcpy(slot.bytes.data(),&invalid,sizeof(invalid)); found=true;
        }
        if(!found) return 5;
    } else if(std::string_view(argv[3])=="schema") {
        value->compiler_schema^=1;
    } else if(std::string_view(argv[3])=="slot") {
        value->frames.front().slots.front().name+="-invalid";
    } else return 6;
    auto encoded=mvvm::codec::encode(*value);
    std::ofstream out(argv[2],std::ios::binary);
    out.write(reinterpret_cast<const char*>(encoded.data()),encoded.size());
    return out?0:7;
}
