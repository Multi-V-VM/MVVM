find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDW REQUIRED IMPORTED_TARGET libdw)
pkg_check_modules(LIBUNWIND REQUIRED IMPORTED_TARGET libunwind)

if(NOT DEFINED WAMR_BUILD_TARGET)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
        set(WAMR_BUILD_TARGET "RISCV64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)")
        set(WAMR_BUILD_TARGET "AARCH64")
    else()
        set(WAMR_BUILD_TARGET "X86_64")
    endif()
endif()

set(WAMR_BUILD_PLATFORM "linux")
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_AOT 1)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_FAST_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_WASI 1)
set(WAMR_BUILD_DUMP_CALL_STACK 1)
set(WAMR_BUILD_COPY_CALL_STACK 1)
set(WAMR_BUILD_AOT_STACK_FRAME 1)
set(WAMR_BUILD_CUSTOM_NAME_SECTION 1)
set(WAMR_BUILD_REF_TYPES 1)

set(WAMR_ROOT_DIR "${PROJECT_SOURCE_DIR}/lib/wasm-micro-runtime")
include("${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")

add_library(mvvm_wamr_runtime STATIC ${WAMR_RUNTIME_LIB_SOURCE})
set_target_properties(mvvm_wamr_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

add_library(mvvm_dwarf_checkpoint STATIC
    "${PROJECT_SOURCE_DIR}/src/dwarf_checkpoint.cpp"
    "${PROJECT_SOURCE_DIR}/src/dwarf_resume.cpp")
target_include_directories(mvvm_dwarf_checkpoint
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
        "${WAMR_ROOT_DIR}/core/iwasm/include")
target_compile_options(mvvm_dwarf_checkpoint PRIVATE
    -g3
    -fno-omit-frame-pointer)
target_link_libraries(mvvm_dwarf_checkpoint
    PUBLIC
        mvvm_wamr_runtime
        PkgConfig::LIBDW
        PkgConfig::LIBUNWIND)

add_executable(mvvm_dwarf_runner "${PROJECT_SOURCE_DIR}/src/dwarf_runner.cpp")
target_compile_options(mvvm_dwarf_runner PRIVATE -g3 -fno-omit-frame-pointer)
target_link_options(mvvm_dwarf_runner PRIVATE -Wl,--export-dynamic)
target_link_libraries(mvvm_dwarf_runner PRIVATE mvvm_dwarf_checkpoint dl pthread m)

option(MVVM_BUILD_DWARF_TESTS "Build DWARF checkpoint unit tests" ON)
if(MVVM_BUILD_DWARF_TESTS)
    add_executable(mvvm_dwarf_snapshot_mutator "${PROJECT_SOURCE_DIR}/test/dwarf_snapshot_mutator.cpp")
    target_include_directories(mvvm_dwarf_snapshot_mutator PRIVATE "${PROJECT_SOURCE_DIR}/include")
    add_executable(mvvm_dwarf_checkpoint_test
        "${PROJECT_SOURCE_DIR}/test/dwarf_checkpoint_test.cpp")
    target_compile_options(mvvm_dwarf_checkpoint_test PRIVATE
        -g3
        -fno-omit-frame-pointer)
    target_link_libraries(mvvm_dwarf_checkpoint_test
        PRIVATE mvvm_dwarf_checkpoint)
    add_test(NAME mvvm_dwarf_checkpoint_round_trip
             COMMAND mvvm_dwarf_checkpoint_test)
endif()
