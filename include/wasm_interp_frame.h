//
// Created by yiwei yang on 5/1/23.
//

#ifndef MVVM_WASM_INTERP_FRAME_H
#define MVVM_WASM_INTERP_FRAME_H
#include "wasm_runtime.h"
#include "wasm_branch_block.h"
struct WAMRInterpFrame {
    /* Instruction pointer of the bytecode array.  */
    std::unique_ptr<uint8>ip;

    //#if WASM_ENABLE_FAST_JIT != 0
    //    uint8 *jitted_return_addr;
    //#endif

//#if WASM_ENABLE_PERF_PROFILING != 0
//    uint64 time_started;
//#endif

    //#if WASM_ENABLE_FAST_INTERP != 0
    //    /* Return offset of the first return value of current frame,
    //   the callee will put return values here continuously */
    //    uint32 ret_offset;
    //    uint32 *lp;
    //    uint32 operand[1];
    //#else
    /* Operand stack top pointer of the current frame. The bottom of
       the stack is the next cell after the last local variable. */

    uint32 stack_size;
    std::unique_ptr<uint32> sp; // all the sp that can be restart

    uint32 csp_size;
    std::unique_ptr<WAMRBranchBlock> csp;

    /**
     * Frame data, the layout is:
     *  lp: parameters and local variables
     *  sp_bottom to sp_boundary: wasm operand stack
     *  csp_bottom to csp_boundary: wasm label stack
     *  jit spill cache: only available for fast jit
     */
    uint32 lp[1];
    //#endif
};
#endif // MVVM_WASM_INTERP_FRAME_H
