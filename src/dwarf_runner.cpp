// Native AOT/DWARF experiment runner. The debug ELF uses the same WAMR ABI as
// its companion AOT container; this preserves DWARF for libdwfl discovery.
#include "mvvm/dwarf_resume.hpp"
#include "mvvm/reflection_codec.hpp"
#include "aot_runtime.h"
#include <dlfcn.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <format>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using Clock=std::chrono::steady_clock;
using mvvm::dwarf::NativeContinuation;
NativeContinuation saved;
std::string identity, callback_error;
std::uint64_t checkpoint_index=1, checkpoint_count=0;
bool saving=false;
double capture_ms=0;
std::vector<char> read_file(const char* path) {
    std::ifstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error(std::format("cannot open {}",path));
    return {std::istreambuf_iterator<char>(f),{}};
}
void checkpoint(wasm_exec_env_t env) {
    try {
        if(mvvm::dwarf::finish_native_rewind()) return;
        if(!saving || ++checkpoint_count!=checkpoint_index) return;
        auto t=Clock::now();
        auto* inst=reinterpret_cast<AOTModuleInstance*>(wasm_runtime_get_module_inst(env));
        if(inst->memory_count!=1) throw std::runtime_error("experiment requires one linear memory");
        auto* mem=inst->memories[0];
        saved.module_identity=identity;
        saved.frames=mvvm::dwarf::capture_native_continuation();
        saved.memory.assign(reinterpret_cast<std::byte*>(mem->memory_data),reinterpret_cast<std::byte*>(mem->memory_data)+mem->memory_data_size);
        saved.globals.assign(reinterpret_cast<std::byte*>(inst->global_data),reinterpret_cast<std::byte*>(inst->global_data)+inst->global_data_size);
        saved.tables.clear();
        for(std::uint32_t i=0;i<inst->table_count;++i) {
            auto* table=inst->tables[i];
            saved.tables.emplace_back(table->elems,table->elems+table->cur_size);
        }
        mvvm::dwarf::request_native_suspend();
        capture_ms=std::chrono::duration<double,std::milli>(Clock::now()-t).count();
    } catch(const std::exception& e) {
        callback_error=e.what();
        wasm_runtime_set_exception(wasm_runtime_get_module_inst(env),e.what());
        mvvm::dwarf::request_native_suspend();
    }
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=8) throw std::runtime_error("usage: runner wasm aot debug.so baseline|save|restore snapshot iterations checkpoint-index");
        auto wasm=read_file(argv[1]), aot=read_file(argv[2]);
        identity.assign(wasm.begin(),wasm.end());
        std::string mode=argv[4];
        if(mode!="baseline" && mode!="save" && mode!="restore") throw std::runtime_error("invalid mode");
        saving=mode=="save"; checkpoint_index=std::stoull(argv[7]);
        if(!checkpoint_index) throw std::runtime_error("checkpoint index starts at one");
        auto iterations=static_cast<std::uint32_t>(std::stoul(argv[6]));
        NativeSymbol symbol{const_cast<char*>("mvvm_checkpoint"),reinterpret_cast<void*>(checkpoint),"()",nullptr};
        RuntimeInitArgs init{};
        init.mem_alloc_type=Alloc_With_Allocator;
        init.mem_alloc_option.allocator.malloc_func=reinterpret_cast<void*>(malloc);
        init.mem_alloc_option.allocator.realloc_func=reinterpret_cast<void*>(realloc);
        init.mem_alloc_option.allocator.free_func=reinterpret_cast<void*>(free);
        init.native_module_name="env"; init.native_symbols=&symbol; init.n_native_symbols=1;
        if(!wasm_runtime_full_init(&init)) throw std::runtime_error("WAMR init failed");
        char error[512]{};
        auto module=wasm_runtime_load(reinterpret_cast<std::uint8_t*>(aot.data()),aot.size(),error,sizeof(error));
        if(!module) throw std::runtime_error(error);
        auto* am=reinterpret_cast<AOTModule*>(module);
        void* dso=nullptr;
        if(std::string_view(argv[3])!="-") {
          dso=dlopen(argv[3],RTLD_NOW|RTLD_LOCAL);
          if(!dso) throw std::runtime_error(dlerror());
          for(std::uint32_t i=0;i<am->func_count;++i) {
            auto name=std::format("aot_func#{}",i);
            auto* f=dlsym(dso,name.c_str());
            if(!f) throw std::runtime_error(std::format("missing debug native function {}",name));
            am->func_ptrs[i]=f;
          }
        } else if(mode!="baseline") {
            throw std::runtime_error("save/restore require native debug ELF");
        }
        if(am->start_function) throw std::runtime_error("experiment does not support module start functions");
        auto instance=wasm_runtime_instantiate(module,1<<20,0,error,sizeof(error));
        if(!instance) throw std::runtime_error(error);
        auto env=wasm_runtime_create_exec_env(instance,1<<20);
        if(!env) throw std::runtime_error("cannot create execution environment");
        auto* inst=reinterpret_cast<AOTModuleInstance*>(instance);
        double decode_ms=0;
        if(mode=="restore") {
            auto bytes=read_file(argv[5]); auto t=Clock::now();
            auto decoded=mvvm::codec::decode<NativeContinuation>(std::as_bytes(std::span(bytes)));
            if(!decoded) throw std::runtime_error(decoded.error());
            saved=std::move(*decoded);
            if(saved.module_identity!=identity) throw std::runtime_error("snapshot Wasm identity mismatch");
            if(inst->memory_count!=1 || saved.memory.size()!=inst->memories[0]->memory_data_size || saved.globals.size()!=inst->global_data_size) throw std::runtime_error("snapshot memory/global layout mismatch");
            if(saved.tables.size()!=inst->table_count) throw std::runtime_error("snapshot table count mismatch");
            for(std::uint32_t i=0;i<inst->table_count;++i) {
                auto* table=inst->tables[i];
                if(saved.tables[i].size()!=table->cur_size) throw std::runtime_error("grown tables not supported by experiment runner");
                for(auto elem:saved.tables[i]) if(elem!=UINT32_MAX && elem>=am->func_count+am->import_func_count) throw std::runtime_error("invalid table function index");
            }
            std::memcpy(inst->memories[0]->memory_data,saved.memory.data(),saved.memory.size());
            std::memcpy(inst->global_data,saved.globals.data(),saved.globals.size());
            for(std::uint32_t i=0;i<inst->table_count;++i) std::copy(saved.tables[i].begin(),saved.tables[i].end(),inst->tables[i]->elems);
            mvvm::dwarf::arm_native_restore(saved.frames);
            decode_ms=std::chrono::duration<double,std::milli>(Clock::now()-t).count();
        }
        auto function=wasm_runtime_lookup_function(instance,"run");
        if(!function) throw std::runtime_error("missing run export");
        std::uint32_t args[2]{iterations,0}; auto t=Clock::now();
        bool ok=wasm_runtime_call_wasm(env,function,1,args);
        double run_ms=std::chrono::duration<double,std::milli>(Clock::now()-t).count();
        if(!callback_error.empty()) throw std::runtime_error(callback_error);
        if(!ok) throw std::runtime_error(wasm_runtime_get_exception(instance));
        if(mvvm::dwarf::native_rewind_pending()) throw std::runtime_error("execution ended before native rewind completed");
        std::size_t bytes_written=0; double encode_ms=0;
        if(saving) {
            if(saved.frames.empty()) throw std::runtime_error("checkpoint did not capture native AOT frames");
            auto et=Clock::now(); auto encoded=mvvm::codec::encode(saved);
            encode_ms=std::chrono::duration<double,std::milli>(Clock::now()-et).count();
            bytes_written=encoded.size(); std::ofstream out(argv[5],std::ios::binary|std::ios::trunc);
            out.write(reinterpret_cast<const char*>(encoded.data()),encoded.size());
            if(!out) throw std::runtime_error("snapshot write failed");
        }
        auto result=args[0]; std::uint32_t effects[2]{};
        double restore_ms=mvvm::dwarf::native_restore_time_ms();
        mvvm::dwarf::reset_native_continuation();
        auto ef=wasm_runtime_lookup_function(instance,"effects");
        if(ef && !wasm_runtime_call_wasm(env,ef,0,effects)) throw std::runtime_error("effects export failed");
        std::size_t slots=0; for(const auto& f:saved.frames) slots+=f.slots.size();
        std::cout<<std::format("{{\"mode\":\"{}\",\"iterations\":{},\"result\":{},\"effects\":{},\"frames\":{},\"slots\":{},\"bytes\":{},\"run_ms\":{:.6f},\"capture_ms\":{:.6f},\"encode_ms\":{:.6f},\"decode_ms\":{:.6f},\"stack_restore_ms\":{:.6f}}}\n",mode,iterations,result,effects[0],saved.frames.size(),slots,bytes_written,run_ms,capture_ms,encode_ms,decode_ms,restore_ms);
        wasm_runtime_destroy_exec_env(env); wasm_runtime_deinstantiate(instance);
        wasm_runtime_unload(module); if(dso) dlclose(dso); wasm_runtime_destroy();
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
