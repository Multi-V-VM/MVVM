/*
 * The WebAssembly Live Migration Project
 *
 * SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 */

#ifndef MVVM_JS_API_H
#define MVVM_JS_API_H

#include "mvvm_export.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

MVVM_API const char *mvvm_backend_name(void);
MVVM_API const char *mvvm_last_error(void);

MVVM_API int mvvm_checkpoint(const char *target, const char *checkpoint_path, const char *dir, const char *map_dir,
                             const char *env, const char *arg, const char *addr, const char *ns_pool, int jit,
                             uint64_t count, int function_index, int function_count, int debug,
                             uint32_t signal_after_ms, uint32_t timeout_ms);

MVVM_API int mvvm_restore(const char *target, const char *checkpoint_path, int jit, uint64_t count,
                          uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // MVVM_JS_API_H
