#pragma once
#include <stddef.h>
#define HAS_FLOAT 1
#define HAS_STDIO 0
#define HAS_PRINTF 0
#define MULTITHREAD 1
#define SEED_METHOD SEED_VOLATILE
#define MEM_METHOD MEM_STATIC
#define CORE_DEBUG 0
typedef signed short ee_s16;
typedef unsigned short ee_u16;
typedef signed int ee_s32;
typedef unsigned int ee_u32;
typedef unsigned char ee_u8;
typedef double ee_f32;
typedef __UINTPTR_TYPE__ ee_ptr_int;
typedef size_t ee_size_t;
typedef unsigned long long CORE_TICKS;
typedef struct { ee_u8 portable_id; } core_portable;
#define align_mem(x) ((void*)(4+(((ee_ptr_int)(x)-1)&~(ee_ptr_int)3)))
int ee_printf(const char*,...);
