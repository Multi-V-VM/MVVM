//
// Created by victoryang00 on 5/1/23.
//

#ifndef MVVM_WAMR_EXEC_ENV_H
#define MVVM_WAMR_EXEC_ENV_H
#include "wamr_block_addr.h"
#include "wamr_interp_frame.h"
#include "wamr_module_instance.h"
#include "wasm_runtime.h"
#include <memory>
#include <ranges>
#include <tuple>
#include <vector>

struct WAMRExecEnv { // multiple
    /* Next thread's exec env of a WASM module instance. we can get the previous exec env outside layer */
    //    struct WASMExecEnv *next;
    //
    //    /* Previous thread's exec env of a WASM module instance. */
    //    struct WASMExecEnv *prev;
    uint8 cur_count{};

    /* Note: field module_inst, argv_buf, native_stack_boundary,
       susƒend_flags, aux_stack_boundary, aux_stack_bottom, and
       native_symbol are used by AOTed code, don't change the
       places of them */

    /* The WASM module instance of current thread */
    WAMRModuleInstance module_inst{};

    // #if WASM_ENABLE_AOT != 0
    //     uint32 *argv_buf;
    // #endif

    /* The boundary of native stack. When runtime detects that native
       frame may overrun this boundary, it throws stack overflow
       exception. */
    //    uint8 *native_stack_boundary;

    /* Used to terminate or suspend current thread
        bit 0: need to terminate
        bit 1: need to suspend
        bit 2: need to go into breakpoint
        bit 3: return from pthread_exit */
    uint32 flags;

    /* Auxiliary stack boundary */
    uint32 aux_boundary;
    /* Auxiliary stack bottom */
    uint32 aux_bottom;

    // #if WASM_ENABLE_AOT != 0
    //     /* Native symbol list, reserved */
    //     void **native_symbol;
    // #endif

    /*
     * The lowest stack pointer value observed.
     * Assumption: native stack grows to the lower address.
     */
    //    uint8 *native_stack_top_min;

    // #if WASM_ENABLE_FAST_JIT != 0
    //     /**
    //      * Cache for
    //      * - jit native operations in 32-bit target which hasn't 64-bit
    //      *   int/float registers, mainly for the operations of double and int64,
    //      *   such as F64TOI64, F32TOI64, I64 MUL/REM, and so on.
    //      * - SSE instructions.
    //      **/
    //     uint64 jit_cache[2];
    // #endif

    // #if WASM_ENABLE_THREAD_MGR != 0
    //     /* thread return value */
    //     void *thread_ret_value;
    //
    //     /* Must be provided by thread library */
    //     void *(*thread_start_routine)(void *);
    //     void *thread_arg;
    //
    //     /* pointer to the cluster */
    //     WASMCluster *cluster;
    //
    //     /* used to support debugger */
    //     korp_mutex wait_lock;
    //     korp_cond wait_cond;
    //     /* the count of threads which are joining current thread */
    //     uint32 wait_count;
    //
    //     /* whether current thread is detached */
    //     bool thread_is_detached;
    // #endif

    // #if WASM_ENABLE_DEBUG_INTERP != 0
    //     WASMCurrentEnvStatus *current_status;
    // #endif

    /* attachment for native function */
    //    void *attachment;

    //    void *user_data;

    /* Current interpreter frame of current thread */
    std::vector<std::unique_ptr<WAMRInterpFrame>> frames;

    /* The native thread handle of current thread */
    //    korp_tid handle;

#if WASM_ENABLE_INTERP != 0
    WAMRBlockAddr block_addr_cache[BLOCK_ADDR_CACHE_SIZE][BLOCK_ADDR_CONFLICT_SIZE];
#endif

    // #ifdef OS_ENABLE_HW_BOUND_CHECK
    //     WASMJmpBuf *jmpbuf_stack_top;
    //     /* One guard page for the exception check */
    //     uint8 *exce_check_guard_page;
    // #endif

    // #if WASM_ENABLE_MEMORY_PROFILING != 0
    //     uint32 max_wasm_stack_used;
    // #endif

    /* The WASM stack size */
    //    uint32 wasm_stack_size;
    /* The WASM stack of current thread */
    //    union {
    //        uint64 __make_it_8_byte_aligned_;
    //
    //        struct {
    //            /* The top boundary of the stack. */
    //            uint8 *top_boundary;
    //
    //            /* Top cell index which is free. */
    //            uint8 *top;
    //
    //            /* The WASM stack. */
    //            uint8 bottom[1];
    //        } s;
    //    } wasm_stack;
    std::vector<uint8_t> wasm_stack; // not known in the compile time

    void dump_impl(WASMExecEnv *env) {
        dump(&this->module_inst, reinterpret_cast<WASMModuleInstance *>(env->module_inst));
        flags = env->suspend_flags.flags;
        aux_boundary = env->aux_stack_boundary.boundary;
        aux_bottom = env->aux_stack_bottom.bottom;
        for (int i = 0; i < BLOCK_ADDR_CACHE_SIZE; i++) {
            for (int j = 0; j < 2; j++) {
                dump(&(block_addr_cache[i][j]), &(env->block_addr_cache[i][j]));
            }
        }
        auto cur_frame = env->cur_frame;
        while (cur_frame) {
            auto dumped_frame = new WAMRInterpFrame();
            dump(dumped_frame, cur_frame);
            this->frames.emplace_back(dumped_frame);
            //            this->lp.emplace_back(((uint8*)cur_frame->lp)- env->wasm_stack.s.bottom);
            //            LOGV(DEBUG)<<"lp:"<<this->lp.back() << " " << ((uint8*)cur_frame)-env->wasm_stack.s.bottom;
            cur_frame = cur_frame->prev_frame;
        }
        wasm_stack = std::vector(env->wasm_stack.s.bottom, env->wasm_stack.s.top);
    };

    void restore_impl(WASMExecEnv *env) {
        restore(&this->module_inst, reinterpret_cast<WASMModuleInstance *>(env->module_inst));
        env->suspend_flags.flags = flags;
        env->aux_stack_boundary.boundary = aux_boundary;
        env->aux_stack_bottom.bottom = aux_bottom;
        /** Need to make sure up to this point wasm_stack is allocated */
        memcpy(env->wasm_stack.s.bottom, wasm_stack.data(), wasm_stack.size());
        env->wasm_stack.s.top = env->wasm_stack.s.bottom + wasm_stack.size();
        /** Should comply to the original wasm_stack */
        LOGV(DEBUG) << (uint32)offsetof(WASMInterpFrame, lp);
        env->cur_frame = (WASMInterpFrame *)((uint8 *)env->wasm_stack.s.bottom + this->frames[0]->lp -
                                             (uint32)offsetof(WASMInterpFrame, lp));
        auto cur_frame = env->cur_frame;
#if defined(__DARWIN__) || defined(__APPLE__) || defined(_WIN32)
        for (int i=0; i<this->frames.size(); i++) {
            restore(this->frames[i].get(), cur_frame);
            if (i != this->frames.size()-1) {
                cur_frame->prev_frame = (WASMInterpFrame *)((uint8 *)env->wasm_stack.s.bottom + this->frames[i+1]->lp -
                                                            (uint32)offsetof(WASMInterpFrame, lp));
                LOGV(DEBUG) << "cur_frame" << (void *)cur_frame << " " << this->frames[i+1]->lp;
                cur_frame = cur_frame->prev_frame;
            }
        }
#else
        for (auto &&[frame, next] : this->frames | std::views::adjacent<2>) {
            restore(frame.get(), cur_frame);
            if (frame != this->frames.back()) {
                cur_frame->prev_frame = (WASMInterpFrame *)((uint8 *)env->wasm_stack.s.bottom + next->lp -
                                                            (uint32)offsetof(WASMInterpFrame, lp));
                LOGV(DEBUG) << "cur_frame" << (void *)cur_frame << " " << next->lp;
                cur_frame = cur_frame->prev_frame;
            }
        }
#endif
        for (int i = 0; i < BLOCK_ADDR_CACHE_SIZE; i++) {
            for (int j = 0; j < BLOCK_ADDR_CONFLICT_SIZE; j++) {
                restore(&(block_addr_cache[i][j]), &(env->block_addr_cache[i][j]));
            }
        }
    };
};

template <SerializerTrait<WASMExecEnv *> T> void dump(T t, WASMExecEnv *env) { t->dump_impl(env); }
template <SerializerTrait<WASMExecEnv *> T> void restore(T t, WASMExecEnv *env) { t->restore_impl(env); }

#endif // MVVM_WAMR_EXEC_ENV_H
