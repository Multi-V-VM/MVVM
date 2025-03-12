/*
 * The WebAssembly Live Migration Project
 *
 *  By: Aibo Hu
 *      Yiwei Yang
 *      Brian Zhao
 *      Andrew Quinn
 *
 *  Copyright 2024 Regents of the Univeristy of California
 *  UC Santa Cruz Sluglab.
 */

#include "wamr_exec_env.h"
#include "aot_runtime.h"
#include "wamr.h"

extern WAMRInstance *wamr;
void WAMRExecEnv::dump_impl(WASMExecEnv *env) {
    this->cur_count = ((uint64_t)env->handle);
    dump(&this->module_inst, reinterpret_cast<WASMModuleInstance *>(env->module_inst));
    flags = env->suspend_flags.flags;
    aux_boundary = env->aux_stack_boundary.boundary;
    aux_bottom = env->aux_stack_bottom.bottom;
    auto cur_frame = env->cur_frame;
    while (cur_frame) {
        auto dumped_frame = new WAMRInterpFrame();
        if (wamr->is_aot) {
            dump(dumped_frame, (AOTFrame *)cur_frame);
        } else {
            dump(dumped_frame, cur_frame);
        }
        this->frames.emplace_back(dumped_frame);
        cur_frame = cur_frame->prev_frame;
    }
}
void WAMRExecEnv::restore_impl(WASMExecEnv *env) {
    env->suspend_flags.flags = flags;
    env->aux_stack_boundary.boundary = aux_boundary;
    env->aux_stack_bottom.bottom = aux_bottom;
    restore(&this->module_inst, reinterpret_cast<WASMModuleInstance *>(env->module_inst));

    if (wamr->is_aot) {
        std::reverse(frames.begin(), frames.end());

        auto save_top = env->wasm_stack.top;
        env->call_chain_size = 0;
        for (const auto &dump_frame : frames) {
            if (!aot_alloc_frame(env, dump_frame->function_index)) {
                SPDLOG_DEBUG("restore error: aot_alloc_frame failed");
                exit(-1);
            }
            auto cur_frame = (AOTFrame *)env->cur_frame;
            cur_frame->ip_offset = dump_frame->ip;
            cur_frame->sp = cur_frame->lp + dump_frame->sp;
            fprintf(stderr, "restore function_index %d\n", dump_frame->function_index);
            cur_frame->func_index = dump_frame->function_index;
            memcpy(cur_frame->lp, dump_frame->stack_frame.data(), dump_frame->stack_frame.size() * sizeof(uint32));
        }
        env->cur_frame = nullptr;
        env->wasm_stack.top = save_top;
        env->call_chain_size = frames.size();
    } else {
        auto module_inst = (WASMModuleInstance *)env->module_inst;
        auto get_function = [&](size_t function_index) {
            if (0 <= function_index && function_index < module_inst->e->function_count) {
                SPDLOG_DEBUG("function_index {} restored", function_index);
                auto function = &module_inst->e->functions[function_index];
                if (function->is_import_func) {
                    SPDLOG_ERROR("function_index {} is_import_func", function_index);
                    exit(-1);
                }
                return function;
            } else {
                SPDLOG_ERROR("function_index {} invalid", function_index);
                exit(-1);
            };
        };

        auto ALLOC_FRAME = [&](WASMExecEnv *exec_env, uint32 size, WASMInterpFrame *prev_frame) {
            auto frame = (WASMInterpFrame *)wasm_exec_env_alloc_wasm_frame(exec_env, size);
            if (frame) {
                frame->prev_frame = prev_frame;
            } else {
                SPDLOG_ERROR("wasm operand stack overflow");
                exit(-1);
            }
            return frame;
        };

        std::reverse(frames.begin(), frames.end());
        auto function = get_function(frames.at(0)->function_index);
        unsigned all_cell_num = function->ret_cell_num > 2 ? function->ret_cell_num : 2;
        unsigned frame_size = wasm_interp_interp_frame_size(all_cell_num);
        WASMRuntimeFrame *prev_frame = nullptr;
        // dummy frame
        WASMRuntimeFrame *frame = ALLOC_FRAME(env, frame_size, prev_frame);
        frame->ip = nullptr;
        prev_frame = frame;
        for (const auto &dump_frame : frames) {
            auto cur_func = get_function(dump_frame->function_index);
            auto cur_wasm_func = cur_func->u.func;
            all_cell_num = cur_func->param_cell_num + cur_func->local_cell_num + cur_wasm_func->max_stack_cell_num +
                           cur_wasm_func->max_block_num * (uint32)sizeof(WASMBranchBlock) / 4;
            frame_size = wasm_interp_interp_frame_size(all_cell_num);
            if (!(frame = ALLOC_FRAME(env, frame_size, prev_frame))) {
                SPDLOG_ERROR("ALLOC_FRAME failed");
                exit(-1);
            }
            restore(dump_frame.get(), frame);
            prev_frame = frame;
        }
        env->cur_frame = prev_frame;
    }
    env->handle = ((korp_tid)this->cur_count);
}
