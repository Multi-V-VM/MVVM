/*
 * Windows Platform Configuration for WAMR
 */

#ifndef WAMR_PLATFORM_WINDOWS_H
#define WAMR_PLATFORM_WINDOWS_H

#ifdef _WIN32

// Define Windows platform before including WAMR headers
#ifndef BH_PLATFORM_WINDOWS
#define BH_PLATFORM_WINDOWS
#endif

// Prevent inclusion of winsock.h - we want winsock2.h
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Include WAMR platform headers
// Note: platform_internal.h will include windows.h and winsock2.h in the correct order
#include "../lib/wasm-micro-runtime/core/shared/platform/windows/platform_internal.h"
#else
#include "platform_common.h"
#include "platform_api_vmcore.h"
#endif // _WIN32

#endif // WAMR_PLATFORM_WINDOWS_H