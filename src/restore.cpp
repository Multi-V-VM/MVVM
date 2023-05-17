//
// Created by victoryang00 on 4/29/23.
//

#include <iostream>
#include <string>

#include "struct_pack/struct_pack.hpp"
#include "wamr.h"
#include "wamr_exec_env.h"
#include "wamr_read_write.h"

auto reader = fread_stream("test.bin");

int main() {
    //  first get the deserializer message, here just hard code
    auto a = struct_pack::deserialize<std::vector<WAMRExecEnv>>(reader).value();
    auto wamr = new WAMRInstance(a);

    wamr->invoke_main();
    return 0;
}