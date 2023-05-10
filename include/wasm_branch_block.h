//
// Created by yiwei yang on 5/2/23.
//

#ifndef MVVM_WAMR_BRANCH_BLOCK_H
#define MVVM_WAMR_BRANCH_BLOCK_H
#include "wasm_interp_frame.h"
#include "wasm_runtime.h"
#include "wamr_serializer.h"
#include <memory>
struct WAMRBranchBlock {
    std::unique_ptr<uint8> begin_addr;
    std::unique_ptr<uint8> target_addr;
    std::unique_ptr<uint32> frame_sp;
    uint32 cell_num;

    void dump(WASMBranchBlock *env);
    void restore(WASMBranchBlock *env);
};
template <SerializerTrait<WASMBranchBlock *> T> void dump(T &t, WASMBranchBlock *env) { t->dump(env); }

template <SerializerTrait<WASMBranchBlock *> T> void restore(T &t, WASMBranchBlock *env) { t->restore(env); }
#endif // MVVM_WAMR_BRANCH_BLOCK_H
