// CoreMark algorithm workload with deterministic checkpoint hooks. Timing is
// measured by the native host runner; this is not an EEMBC certified score.
#include "coremark.h"
#ifdef __wasm__
__attribute__((import_module("env"),import_name("mvvm_checkpoint"))) void mvvm_checkpoint(void);
#else
void mvvm_checkpoint(void) {}
#endif
static core_results result;
static unsigned char memory[2000];
static volatile unsigned effects_count;
volatile ee_s32 seed1_volatile=0,seed2_volatile=0,seed3_volatile=0x66,seed4_volatile=0,seed5_volatile=7;
int ee_printf(const char* format,...) {(void)format;return 0;}
void *memset(void* dst,int value,size_t n) { unsigned char* p=dst; while(n--) *p++=value; return dst; }
void mvvm_coremark_checkpoint(void) { ++effects_count; mvvm_checkpoint(); }
__attribute__((export_name("run"))) unsigned run(unsigned iterations) {
    memset(&result,0,sizeof(result));
    result.seed1=seed1_volatile; result.seed2=seed2_volatile; result.seed3=seed3_volatile;
    result.size=2000/3; result.execs=ALL_ALGORITHMS_MASK;
    result.iterations=iterations;
    for(unsigned i=0;i<4;++i) result.memblock[i]=memory+(i?i-1:0)*result.size;
    result.list=core_list_init(result.size,result.memblock[1],result.seed1);
    core_init_matrix(result.size,result.memblock[2],result.seed1|((ee_s32)result.seed2<<16),&result.mat);
    core_init_state(result.size,result.seed1,result.memblock[3]);
    for(unsigned i=0;i<iterations;++i) {
        ee_u16 crc=core_bench_list(&result,1); result.crc=crcu16(crc,result.crc);
        crc=core_bench_list(&result,-1); result.crc=crcu16(crc,result.crc);
        if(i==0) result.crclist=result.crc;
    }
    // Validate the upstream 2K performance-seed CRCs on every run.
    if(result.crclist!=0xe714 || result.crcmatrix!=0x1fd7 || result.crcstate!=0x8e3a) return 0xffffffff;
    return result.crc;
}
__attribute__((export_name("effects"))) unsigned effects(void) { return effects_count; }
