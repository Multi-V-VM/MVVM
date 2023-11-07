//
// Created by victoryang00 on 4/29/23.
//

#include "struct_pack/struct_pack.hpp"
#include "wamr.h"
#include "wamr_exec_env.h"
#include "wamr_read_write.h"
#include "wasm_runtime.h"
#include <cxxopts.hpp>
#include <iostream>
#include <memory>
#include <string>

FreadStream *reader;
WAMRInstance *wamr = nullptr;
void serialize_to_file(WASMExecEnv *instance) {}
int main(int argc, char **argv) {
    cxxopts::Options options("MVVM", "Migratable Velocity Virtual Machine, to ship the VM state to another machine");
    options.add_options()("t,target", "The webassembly file to execute",
                          cxxopts::value<std::string>()->default_value("./test/counter.wasm"))(
        "j,jit", "Whether the jit mode or interp mode", cxxopts::value<bool>()->default_value("false"))(
        "h,help", "The value for epoch value", cxxopts::value<bool>()->default_value("false"));
    // Can first discover from the wasi context.
    auto removeExtension = [](std::string &filename) {
        size_t dotPos = filename.find_last_of('.');
        std::string res;
        if (dotPos != std::string::npos) {
            // Extract the substring before the period
            res = filename.substr(0, dotPos);
        } else {
            // If there's no period in the string, it means there's no extension.
            LOGV(ERROR) << "No extension found.";
        }
        return res;
    };

    auto result = options.parse(argc, argv);
    if (result["help"].as<bool>()) {
        std::cout << options.help() << std::endl;
        exit(0);
    }
    auto target = result["target"].as<std::string>();
    reader = new FreadStream((removeExtension(target) + ".bin").c_str());
    wamr = new WAMRInstance(target.c_str(), false);
    auto a = struct_pack::deserialize<std::vector<std::unique_ptr<WAMRExecEnv>>>(*reader).value();
    wamr->instantiate();
    wamr->recover(&a);
    return 0;
}