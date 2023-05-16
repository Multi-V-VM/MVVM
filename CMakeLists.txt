cmake_minimum_required(VERSION 3.22)
project(MVVM LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)

find_package(FMT REQUIRED)
find_package(LLVM REQUIRED)

set(WAMR_BUILD_PLATFORM "linux")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
add_compile_options (-fdiagnostics-color=always)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
add_compile_options (-fcolor-diagnostics)
endif ()

# Reset default linker flags
set(CMAKE_C_FLAGS "-Wno-implicit-function-declaration")
set(CMAKE_Cxx_FLAGS "-Wno-implicit-function-declaration")
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

# Set WAMR_BUILD_TARGET, currently values supported:
# "X86_64", "AMD_64", "X86_32", "AARCH64[sub]", "ARM[sub]", "THUMB[sub]",
# "MIPS", "XTENSA", "RISCV64[sub]", "RISCV32[sub]"
if (NOT DEFINED WAMR_BUILD_TARGET)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)")
        set(WAMR_BUILD_TARGET "AARCH64")
    elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
        set(WAMR_BUILD_TARGET "RISCV64")
    elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
        # Build as X86_64 by default in 64-bit platform
        set(WAMR_BUILD_TARGET "X86_64")
    elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
        # Build as X86_32 by default in 32-bit platform
        set(WAMR_BUILD_TARGET "X86_32")
    else ()
        message(SEND_ERROR "Unsupported build target platform!")
    endif ()
endif ()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif ()

set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_AOT 1)
set(WAMR_BUILD_JIT 1)
set(WASM_ENABLE_DEBUG_AOT 0)
set(WAMR_BUILD_FAST_JIT 0)
set(WAMR_BUILD_WASI_NN 0)
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_WASI 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_LIB_PTHREAD 1)
set(WAMR_BUILD_SIMD 1)

if (NOT DEFINED WAMR_BUILD_DEBUG_INTERP)
    # Disable Debug feature by default
    set(WAMR_BUILD_DEBUG_INTERP 0)
endif ()

if (WAMR_BUILD_DEBUG_INTERP EQUAL 1)
    set(WAMR_BUILD_FAST_INTERP 0)
    set(WAMR_BUILD_MINI_LOADER 0)
    set(WAMR_BUILD_SIMD 0)
endif ()

set(CMAKE_SHARED_LINKER_FLAGS "-Wl,-U,_get_ext_lib_export_apis")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

set(CMAKE_MACOSX_RPATH True)

set(WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/wasm-micro-runtime)

include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
add_library(vmlib ${WAMR_RUNTIME_LIB_SOURCE})

include(${SHARED_DIR}/utils/uncommon/shared_uncommon.cmake)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/lib/yalantinglibs/include)

file(GLOB_RECURSE SOURCE_FILES src/logging.cpp src/wamr*.cpp)

add_executable(MVVM_restore ${SOURCE_FILES} src/restore.cpp  ${UNCOMMON_SHARED_SOURCE})
add_executable(MVVM_checkpoint ${SOURCE_FILES} src/checkpoint.cpp  ${UNCOMMON_SHARED_SOURCE})

target_link_libraries(MVVM_restore fmt::fmt vmlib -lm -ldl -lpthread ${LLVM_AVAILABLE_LIBS})
target_link_libraries(MVVM_checkpoint fmt::fmt vmlib -lm -ldl -lpthread ${LLVM_AVAILABLE_LIBS})
add_definitions(-DMVVM_INTERP=1)