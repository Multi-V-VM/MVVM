//
// Created by root on 5/10/23.
//

#ifndef MVVM_WAMR_JIT_CONTEXT_H
#define MVVM_WAMR_JIT_CONTEXT_H

#include "debug/jit_debug.h"
#include "wamr_interp_frame.h"
#include "wamr_serializer.h"
#include "lldb/API/SBBlock.h"
#include "lldb/API/SBAttachInfo.h"
#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBFileSpecList.h"
#include "lldb/API/SBLaunchInfo.h"
#include "lldb/API/SBSymbolContextList.h"
#include "lldb/API/SBType.h"
#include "lldb/API/SBValue.h"
#include "lldb/API/SBWatchpoint.h"

typedef struct JITCodeEntry {
    struct JITCodeEntry *next_;
    struct JITCodeEntry *prev_;
    const uint8 *symfile_addr_;
    uint64 symfile_size_;
} JITCodeEntry;

struct WAMRJITContext {


    void dump(JITCodeEntry *env){

    };
    void restore(JITCodeEntry *env){

    };
};

template <SerializerTrait<WAMRJITContext *> T> void dump(T t, JITCodeEntry *env) { t->dump(env); }
template <SerializerTrait<WAMRJITContext *> T> void restore(T t, JITCodeEntry *env) { t->restore(env); }

#endif // MVVM_WAMR_JIT_CONTEXT_H
