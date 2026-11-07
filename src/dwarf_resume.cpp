#define UNW_LOCAL_ONLY
#include <libunwind.h>
#include <dwarf.h>
#include <elfutils/libdwfl.h>
#include "mvvm/dwarf_resume.hpp"
#include <algorithm>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <stdexcept>
#include <unistd.h>
#include <cstdio>
#include <chrono>
#include <bit>
#include <span>
#include <set>

static_assert(std::endian::native==std::endian::little,
              "native continuation slots currently require little-endian ISAs");
// The generated code reads this TLS flag without a normal-path helper call.
// 0=running, 1=suspending, 2=rebuilding the destination native call chain.
extern "C" { thread_local std::uint32_t mvvm_dwarf_state=0; }

namespace mvvm::dwarf {
namespace {
thread_local bool suspended=false;
thread_local std::vector<ContinuationFrame> restore_frames;
thread_local std::size_t restore_next=0;
thread_local bool rewinding=false;
thread_local double restore_ms=0;
struct DwflDeleter { void operator()(Dwfl* d)const { if(d) dwfl_end(d); } };
using Debug=std::unique_ptr<Dwfl,DwflDeleter>;
thread_local Debug restore_debug;
Debug open_debug() {
    static const Dwfl_Callbacks cb{.find_elf=dwfl_linux_proc_find_elf,.find_debuginfo=dwfl_standard_find_debuginfo,.section_address=dwfl_offline_section_address,.debuginfo_path=nullptr};
    Debug d(dwfl_begin(&cb));
    if(!d || dwfl_linux_proc_report(d.get(),getpid()) || dwfl_report_end(d.get(),nullptr,nullptr)) throw std::runtime_error("cannot report native DWARF modules");
    return d;
}
// Requirements come from an allocated ELF object, never from DWARF DIEs.
// The inventory has no locations; all value reads/writes still use libdw.
struct FunctionRequirements {
    std::uint32_t sites;
    std::map<std::string,std::size_t> slots;
};
using Requirements=std::map<std::uint32_t,FunctionRequirements>;
thread_local std::map<Dwfl_Module*,Requirements> requirements_cache;
std::span<const std::byte> object_symbol(Dwfl_Module* module,const char* wanted) {
    Dwarf_Addr start=0,end=0;
    dwfl_module_info(module,nullptr,&start,&end,nullptr,nullptr,nullptr,nullptr);
    int count=dwfl_module_getsymtab(module);
    for(int i=0;i<count;++i) {
        GElf_Sym sym{}; GElf_Addr address{}; GElf_Word section{};
        auto* name=dwfl_module_getsym_info(module,i,&sym,&address,&section,nullptr,nullptr);
        if(!name || std::string_view(name)!=wanted) continue;
        if(section==SHN_UNDEF || section==GElf_Word(-1) || GELF_ST_TYPE(sym.st_info)!=STT_OBJECT
           || !sym.st_size || sym.st_size>(16U<<20) || address<start || address>end || sym.st_size>end-address)
            throw std::runtime_error("invalid native continuation manifest symbol");
        return {reinterpret_cast<const std::byte*>(address),std::size_t(sym.st_size)};
    }
    throw std::runtime_error(std::format("missing native continuation manifest symbol {}",wanted));
}
const Requirements& requirements(Dwfl_Module* module) {
    if(auto it=requirements_cache.find(module);it!=requirements_cache.end()) return it->second;
    auto bytes=object_symbol(module,"mvvm_dwarf_requirements");
    auto schema_bytes=object_symbol(module,"mvvm_dwarf_schema_id");
    if(bytes.size()<16 || std::memcmp(bytes.data(),"MVVMREQ3",8) || schema_bytes.size()!=8)
        throw std::runtime_error("invalid native continuation manifest header");
    std::uint64_t schema=0,hash=14695981039346656037ULL;
    std::memcpy(&schema,schema_bytes.data(),8);
    for(auto byte:bytes) { hash^=std::to_integer<unsigned char>(byte); hash*=1099511628211ULL; }
    if(hash!=schema) throw std::runtime_error("native continuation manifest hash mismatch");
    std::size_t at=8;
    auto word=[&]() {
        if(bytes.size()-at<4) throw std::runtime_error("truncated native continuation manifest");
        std::uint32_t v=0;
        for(unsigned i=0;i<4;++i) v|=std::uint32_t(std::to_integer<unsigned char>(bytes[at++]))<<(8*i);
        return v;
    };
    if(word()>1) throw std::runtime_error("unsupported native continuation manifest flags");
    auto count=word();
    if(count>100000) throw std::runtime_error("native continuation manifest too large");
    Requirements result;
    for(std::uint32_t i=0;i<count;++i) {
        auto function=word(),sites=word(),slots=word();
        if(sites>100000 || !slots || slots>100000)
            throw std::runtime_error("invalid native continuation manifest counts");
        for(std::uint32_t j=0;j<sites;++j) (void)word(); // Targets bind the schema, not native addresses.
        FunctionRequirements requirement{sites,{}};
        for(std::uint32_t j=0;j<slots;++j) {
            auto length=word(),width=word();
            if(!length || length>4096 || length>bytes.size()-at || !width || width>8)
                throw std::runtime_error("invalid native continuation manifest slot");
            std::string name(reinterpret_cast<const char*>(bytes.data()+at),length); at+=length;
            if(!name.starts_with("mvvm_") || !requirement.slots.emplace(name,width).second)
                throw std::runtime_error("duplicate or invalid manifest slot name");
        }
        if(!requirement.slots.contains("mvvm_pc") || requirement.slots.at("mvvm_pc")!=4
           || !result.emplace(function,std::move(requirement)).second)
            throw std::runtime_error("invalid manifest function or continuation PC");
    }
    if(at!=bytes.size()) throw std::runtime_error("trailing native continuation manifest bytes");
    return requirements_cache.emplace(module,std::move(result)).first->second;
}
std::uint64_t reg(unw_cursor_t& cursor,unsigned n) {
    unw_word_t v=0;
    if(unw_get_reg(&cursor,n,&v)<0) throw std::runtime_error(std::format("DWARF register {} unavailable",n));
    return v;
}
std::uint64_t expression(Dwarf_Attribute& attr,Dwarf_Addr pc,unw_cursor_t& cursor,std::uint64_t fb,bool memory_location=false) {
    Dwarf_Op* ops=nullptr; std::size_t count=0;
    auto rc=dwarf_getlocation_addr(&attr,pc,&ops,&count,1);
    if(rc!=1 || !count) throw std::runtime_error("DWARF location unavailable at safe point");
    if(count!=1 && !(count==2 && ops[1].atom==DW_OP_plus_uconst)) throw std::runtime_error("unsupported composite DWARF expression");
    std::uint64_t value=0;
    for(std::size_t i=0;i<count;i++) {
        const auto op=ops[i];
        if(memory_location && ((op.atom>=DW_OP_reg0 && op.atom<=DW_OP_reg31) || op.atom==DW_OP_regx)) throw std::runtime_error("continuation value was not spilled to a DWARF memory location");
        if(op.atom>=DW_OP_reg0 && op.atom<=DW_OP_reg31) value=reg(cursor,op.atom-DW_OP_reg0);
        else if(op.atom==DW_OP_regx) value=reg(cursor,op.number);
        else if(op.atom>=DW_OP_breg0 && op.atom<=DW_OP_breg31) value=reg(cursor,op.atom-DW_OP_breg0)+static_cast<std::int64_t>(op.number);
        else if(op.atom==DW_OP_bregx) value=reg(cursor,op.number)+static_cast<std::int64_t>(op.number2);
        else if(op.atom==DW_OP_fbreg) value=fb+static_cast<std::int64_t>(op.number);
        else if(op.atom==DW_OP_call_frame_cfa) {
            auto caller=cursor;
            if(unw_step(&caller)<=0) throw std::runtime_error("DWARF CFA unavailable");
            value=reg(caller,UNW_REG_SP);
        } else if(op.atom==DW_OP_plus_uconst) value+=op.number;
        else throw std::runtime_error(std::format("unsupported DWARF location opcode {:#x}",op.atom));
    }
    return value;
}
struct SlotLocation { std::string name; std::byte* address; std::size_t size; };
std::vector<SlotLocation> locations(Dwfl* d,unw_cursor_t& cursor,std::uint32_t& function,std::uint32_t& sites) {
    auto ip=reg(cursor,UNW_REG_IP);
    auto* module=dwfl_addrmodule(d,ip-1);
    if(!module) return {};
    auto* name=dwfl_module_addrname(module,ip-1);
    if(std::getenv("MVVM_DWARF_TRACE")) std::fprintf(stderr,"unwind ip=%llx sp=%llx name=%s\n",(unsigned long long)ip,(unsigned long long)reg(cursor,UNW_REG_SP),name?name:"?");
    if(!name || !std::string_view(name).starts_with("aot_func#")) return {};
    function=std::stoul(name+9);
    const auto& all=requirements(module);
    auto required=all.find(function);
    if(required==all.end()) throw std::runtime_error("AOT frame absent from continuation manifest");
    const auto& expected=required->second.slots; sites=required->second.sites;
    Dwarf_Addr bias=0;
    auto* cu=dwfl_module_addrdie(module,ip-1,&bias);
    // LLVM may omit .debug_aranges. Scan the CU ranges in that case.
    if(!cu) {
        Dwarf_Die* next=nullptr;
        while((next=dwfl_module_nextcu(module,next,&bias)))
            if(dwarf_haspc(next,ip-1-bias)==1) {cu=next; break;}
    }
    if(!cu) throw std::runtime_error(std::format("AOT code is missing DWARF: {}",dwfl_errmsg(-1)));
    Dwarf_Die* scopes=nullptr;
    int n=dwarf_getscopes(cu,ip-1-bias,&scopes);
    std::unique_ptr<Dwarf_Die,decltype(&std::free)> owned(scopes,std::free);
    Dwarf_Die* sub=nullptr;
    for(int i=0;i<n;i++) if(dwarf_tag(&scopes[i])==DW_TAG_subprogram) { sub=&scopes[i]; break; }
    if(!sub) throw std::runtime_error("AOT DWARF subprogram missing");
    Dwarf_Attribute fb_attr;
    if(!dwarf_attr(sub,DW_AT_frame_base,&fb_attr)) throw std::runtime_error("AOT DWARF frame base missing");
    auto fb=expression(fb_attr,ip-1-bias,cursor,0);
    auto low=reg(cursor,UNW_REG_SP);
    auto caller=cursor;
    if(unw_step(&caller)<=0) throw std::runtime_error("AOT caller CFA missing");
    auto high=reg(caller,UNW_REG_SP);
    // A backend can reuse the incoming stack-argument home for an alloca.
    // Such a DW_OP_fbreg slot belongs to the immediate caller's outgoing
    // argument area (not below this frame's CFA), especially on x86-64.
    auto parent=caller;
    if(unw_step(&parent)>0) high=std::max(high,reg(parent,UNW_REG_SP));
    std::vector<SlotLocation> result;
    std::set<std::string> observed;
    Dwarf_Die child;
    if(dwarf_child(sub,&child)!=0) throw std::runtime_error("AOT continuation slots missing");
    do {
        const char* slotname=dwarf_diename(&child);
        if(dwarf_tag(&child)!=DW_TAG_variable || !slotname || !std::string_view(slotname).starts_with("mvvm_")) continue;
        auto required_slot=expected.find(slotname);
        if(required_slot==expected.end() || !observed.insert(slotname).second)
            throw std::runtime_error("unexpected or duplicate AOT continuation DWARF slot");
        Dwarf_Attribute location,type; Dwarf_Die type_die;
        if(!dwarf_attr(&child,DW_AT_location,&location) || !dwarf_attr(&child,DW_AT_type,&type) || !dwarf_formref_die(&type,&type_die)) throw std::runtime_error("incomplete AOT slot DWARF");
        int size=dwarf_bytesize(&type_die);
        if(size<=0 || std::size_t(size)!=required_slot->second)
            throw std::runtime_error("AOT DWARF slot width differs from continuation manifest");
        auto addr=expression(location,ip-1-bias,cursor,fb,true);
        if(size<=0 || size>8 || addr<low || addr>high || std::uint64_t(size)>high-addr) throw std::runtime_error(std::format("invalid AOT slot {} range {:x} (stack {:x}..{:x})",slotname,addr,low,high));
        result.push_back({slotname,reinterpret_cast<std::byte*>(addr),static_cast<std::size_t>(size)});
    } while(dwarf_siblingof(&child,&child)==0);
    for(const auto& [name,width]:expected)
        if(!observed.contains(name)) throw std::runtime_error(std::format(
            "missing required DWARF slot {} in function {}",name,function));
    if(result.empty()) throw std::runtime_error("empty AOT continuation DWARF");
    return result;
}
}
std::vector<ContinuationFrame> capture_native_continuation() {
    requirements_cache.clear();
    auto d=open_debug(); unw_context_t context; unw_cursor_t cursor;
    unw_getcontext(&context); if(unw_init_local(&cursor,&context)<0) throw std::runtime_error("unwind init failed");
    std::vector<ContinuationFrame> result;
    int step=0;
    do {
        std::uint32_t function=0,sites=0; auto slots=locations(d.get(),cursor,function,sites);
        if(slots.empty()) continue;
        ContinuationFrame frame{function,{}};
        for(const auto& s:slots) {
            if(s.name=="mvvm_pc") {
                std::uint32_t pc=0; std::memcpy(&pc,s.address,4);
                if(!pc || pc>sites) throw std::runtime_error("capture is not at a declared continuation site");
            }
            frame.slots.push_back({s.name,{s.address,s.address+s.size}});
        }
        result.push_back(std::move(frame));
    } while((step=unw_step(&cursor))>0);
    if(step<0) throw std::runtime_error("native continuation unwind failed");
    std::ranges::reverse(result); return result;
}
void arm_native_restore(const std::vector<ContinuationFrame>& frames) {
    if(frames.empty()) throw std::runtime_error("empty native continuation");
    restore_frames=frames; restore_next=0; rewinding=true; suspended=false; restore_ms=0;
    restore_debug.reset(); requirements_cache.clear(); mvvm_dwarf_state=2;
}
bool finish_native_rewind() {
    if(!rewinding) return false;
    if(restore_next!=restore_frames.size()) throw std::runtime_error("checkpoint reached before all frames restored");
    rewinding=false; mvvm_dwarf_state=0; restore_debug.reset(); return true;
}
bool native_suspend_pending(){return suspended;}
bool native_rewind_pending(){return rewinding;}
double native_restore_time_ms(){return restore_ms;}
void request_native_suspend(){suspended=true; mvvm_dwarf_state=1;}
void reset_native_continuation(){suspended=false; rewinding=false; restore_frames.clear(); restore_next=0; mvvm_dwarf_state=0; restore_debug.reset(); requirements_cache.clear();}

std::uint32_t enter(std::uint32_t expected_function,std::uint32_t site_count) {
    if(!rewinding) return 0;
    auto begin=std::chrono::steady_clock::now();
    if(restore_next>=restore_frames.size()) throw std::runtime_error("unexpected extra native frame during restore");
    const auto& source=restore_frames[restore_next];
    if(source.function!=expected_function) throw std::runtime_error("native restore function mismatch");
    if(!restore_debug) restore_debug=open_debug();
    auto& d=restore_debug; unw_context_t context; unw_cursor_t cursor;
    unw_getcontext(&context); unw_init_local(&cursor,&context);
    do {
        std::uint32_t function=0,manifest_sites=0; auto target=locations(d.get(),cursor,function,manifest_sites);
        if(target.empty()) continue;
        if(manifest_sites!=site_count) throw std::runtime_error("native continuation site manifest mismatch");
        if(function!=expected_function || target.size()!=source.slots.size()) throw std::runtime_error("native DWARF schema mismatch");
        std::map<std::string,const SlotValue*> values;
        for(const auto& s:source.slots) if(!values.emplace(s.name,&s).second) throw std::runtime_error("duplicate native slot");
        std::uint32_t pc=0;
        for(const auto& dst:target) {
            auto found=values.find(dst.name);
            if(found==values.end() || found->second->bytes.size()!=dst.size) throw std::runtime_error("native slot width mismatch");
            if(dst.name=="mvvm_pc") {
                if(dst.size!=4) throw std::runtime_error("invalid continuation PC width");
                std::memcpy(&pc,found->second->bytes.data(),4);
            }
        }
        if(!pc || pc>site_count) throw std::runtime_error("invalid continuation PC");
        // Validate every slot before the first write to the new native frame.
        for(const auto& dst:target) std::memcpy(dst.address,values.at(dst.name)->bytes.data(),dst.size);
        ++restore_next;
        restore_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        return pc;
    } while(unw_step(&cursor)>0);
    throw std::runtime_error("cannot locate destination AOT frame");
}
}
extern "C" __attribute__((noinline)) std::uint32_t mvvm_dwarf_enter(void*,std::uint32_t function,std::uint32_t site_count) {return mvvm::dwarf::enter(function,site_count);}
extern "C" void mvvm_dwarf_leave() {}
extern "C" std::uint32_t mvvm_dwarf_pending() {return mvvm::dwarf::native_suspend_pending();}
