//
// Created by yiwei yang on 5/2/23.
//

#ifndef MVVM_WAMR_BRANCH_BLOCK_H
#define MVVM_WAMR_BRANCH_BLOCK_H
#include "wamr_module_instance.h"
#include "wasm_runtime.h"
#include <memory>

struct WAMRBranchBlock {
    uint32 begin_addr; // real code section for native code?WASMBranchBlock
    uint32 target_addr;
    //    std::unique_ptr<uint32> frame_sp;
    uint32 cell_num;

    void dump(WASMBranchBlock *env){};
    void restore(WASMBranchBlock *env){};
};
template <SerializerTrait<WAMRBranchBlock *> T> void dump_branch_block(T &t, WASMBranchBlock *env) { t->dump(env); }

template <SerializerTrait<WAMRBranchBlock *> T> void restore_branch_block(T &t, WASMBranchBlock *env) {
    t->restore(env);
}
#endif // MVVM_WAMR_BRANCH_BLOCK_H