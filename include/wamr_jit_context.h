//
// Created by root on 5/10/23.
//

#ifndef MVVM_WAMR_JIT_CONTEXT_H
#define MVVM_WAMR_JIT_CONTEXT_H

#include "debug/jit_debug.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::orc;

/// Do the registration.
void NotifyDebugger(jit_code_entry *JITCodeEntry) {
    __jit_debug_descriptor.action_flag = JIT_REGISTER_FN;

    // Insert this entry at the head of the list.
    JITCodeEntry->prev_entry = nullptr;
    jit_code_entry *NextEntry = __jit_debug_descriptor.first_entry;
    JITCodeEntry->next_entry = NextEntry;
    if (NextEntry) {
        NextEntry->prev_entry = JITCodeEntry;
    }
    __jit_debug_descriptor.first_entry = JITCodeEntry;
    __jit_debug_descriptor.relevant_entry = JITCodeEntry;
    jit_debug_register_code();
}

struct WAMRJITContext {

    void notifyGDB(){};
    void dump(LLVME *env){};
    void restore(LLVM *env){};
};
template <SerializerTrait<WAMRJITContext *> T> void dump(T t, JITCodeEntry *env) { t->dump(env); }

template <SerializerTrait<WAMRJITContext *> T> void restore(T t, JITCodeEntry *env) { t->restore(env); }
#endif // MVVM_WAMR_JIT_CONTEXT_H
