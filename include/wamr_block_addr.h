//
// Created by victoryang00 on 5/30/23.
//

#ifndef MVVM_WAMR_BLOCK_ADDR_H
#define MVVM_WAMR_BLOCK_ADDR_H

#include "wamr_serializer.h"
#include "wasm_runtime.h"

struct WAMRBlockAddr {
    uint32 start_addr;
    uint32 else_addr;
    uint32 end_addr;

    void dump(BlockAddr *env);
    void restore(BlockAddr *env) const;
};

template <SerializerTrait<BlockAddr *> T> void dump(T t, BlockAddr *env) { t->dump(env); }
template <SerializerTrait<BlockAddr *> T> void restore(T t, BlockAddr *env) { t->restore(env); }

#endif // MVVM_WAMR_BLOCK_ADDR_H
