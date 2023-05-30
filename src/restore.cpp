//
// Created by victoryang00 on 4/29/23.
//

#include "struct_pack/struct_pack.hpp"
#include "wamr.h"
#include "wamr_exec_env.h"
#include "wamr_read_write.h"
#include <iostream>
#include <memory>
#include <string>
#include <cxxopts.hpp>

auto reader = FreadStream("test.bin");
WAMRInstance* wamr = nullptr;

void serialize_to_file(WASMExecEnv *instance) {}

int main(int argc, char **argv) {
    cxxopts::Options options("MVVM", "Migratable Velocity Virtual Machine, to ship the VM state to another machine");
    options.add_options()("t,target", "The webassembly file to execute",
                          cxxopts::value<std::string>()->default_value("./microbench/many_calloc"))(
        "j,jit", "Whether the jit mode or interp mode", cxxopts::value<bool>()->default_value("false"))(
        "h,help", "The value for epoch value", cxxopts::value<bool>()->default_value("false"));

    auto result = options.parse(argc, argv);
    if (result["help"].as<bool>()) {
        std::cout << options.help() << std::endl;
        exit(0);
    }
    auto target = result["target"].as<std::string>();
    wamr = new WAMRInstance(target.c_str(), false);
    //  first get the deserializer message, here just hard code

    auto a = struct_pack::deserialize<std::vector<std::unique_ptr<WAMRExecEnv>>>(reader).value();

    wamr->recover(&a);
    return 0;
}