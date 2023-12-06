add_executable(MVVM_benchmark bench.cpp)
target_link_libraries(MVVM_benchmark MVVM_export vmlib benchmark::benchmark MVVM_export)

if(WIN32)
    set(WASI_SDK "D:/wasi-sdk")
    set(EMSDK_DIR "D:/emsdk/upstream")
    set(WASI_EXE_POSTFIX ".exe")
else()
    set(WASI_SDK "/opt/wasi-sdk")
    set(EMSDK_DIR "/opt/emsdk/upstream")
    set(WASI_EXE_POSTFIX "")
endif()

include(ExternalProject)

ExternalProject_Add(gapbs
        SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/gapbs
        UPDATE_COMMAND    ""
        PATCH_COMMAND     ""
        CONFIGURE_COMMAND  ${CMAKE_COMMAND}
        -DCMAKE_BUILD_TYPE=Release
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        -DCMAKE_C_COMPILER=${WASI_SDK}/bin/clang${WASI_EXE_POSTFIX}
        -DCMAKE_CXX_COMPILER=${WASI_SDK}/bin/clang++${WASI_EXE_POSTFIX}
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--export-all"
        -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
        ${CMAKE_CURRENT_SOURCE_DIR}/gapbs
        BUILD_COMMAND     ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND   ${CMAKE_COMMAND} -E copy_if_different ./bc ${CMAKE_CURRENT_BINARY_DIR}/bc.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./bfs ${CMAKE_CURRENT_BINARY_DIR}/bfs.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./cc ${CMAKE_CURRENT_BINARY_DIR}/cc.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./cc_sv ${CMAKE_CURRENT_BINARY_DIR}/cc_sv.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./converter ${CMAKE_CURRENT_BINARY_DIR}/converter.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./pr ${CMAKE_CURRENT_BINARY_DIR}/pr.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./pr_spmv ${CMAKE_CURRENT_BINARY_DIR}/pr_spmv.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./sssp ${CMAKE_CURRENT_BINARY_DIR}/sssp.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./tc ${CMAKE_CURRENT_BINARY_DIR}/tc.wasm 
)
ExternalProject_Add(gapbs-native
        SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/gapbs
        UPDATE_COMMAND    ""
        PATCH_COMMAND     ""
        CONFIGURE_COMMAND  ${CMAKE_COMMAND}
        -DCMAKE_BUILD_TYPE=Release
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        ${CMAKE_CURRENT_SOURCE_DIR}/gapbs
        BUILD_COMMAND     ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND   ${CMAKE_COMMAND} -E copy_if_different ./bc ${CMAKE_CURRENT_BINARY_DIR}/bc &&
        ${CMAKE_COMMAND} -E copy_if_different ./bfs ${CMAKE_CURRENT_BINARY_DIR}/bfs &&
        ${CMAKE_COMMAND} -E copy_if_different ./cc ${CMAKE_CURRENT_BINARY_DIR}/cc &&
        ${CMAKE_COMMAND} -E copy_if_different ./cc_sv ${CMAKE_CURRENT_BINARY_DIR}/cc_sv &&
        ${CMAKE_COMMAND} -E copy_if_different ./converter ${CMAKE_CURRENT_BINARY_DIR}/converter &&
        ${CMAKE_COMMAND} -E copy_if_different ./pr ${CMAKE_CURRENT_BINARY_DIR}/pr &&
        ${CMAKE_COMMAND} -E copy_if_different ./pr_spmv ${CMAKE_CURRENT_BINARY_DIR}/pr_spmv &&
        ${CMAKE_COMMAND} -E copy_if_different ./sssp ${CMAKE_CURRENT_BINARY_DIR}/sssp &&
        ${CMAKE_COMMAND} -E copy_if_different ./tc ${CMAKE_CURRENT_BINARY_DIR}/tc 
)
# ExternalProject_Add(redis
#        SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/redis
#        UPDATE_COMMAND    ""
#        PATCH_COMMAND     ""
#        CONFIGURE_COMMAND  ${CMAKE_COMMAND}
#        -DCMAKE_BUILD_TYPE=Release
#        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
#        -DCMAKE_C_COMPILER=${WASI_SDK}/bin/clang${WASI_EXE_POSTFIX}
#        -DCMAKE_CXX_COMPILER=${WASI_SDK}/bin/clang++${WASI_EXE_POSTFIX} 
#        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--export-all"
#        -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
#        ${CMAKE_CURRENT_SOURCE_DIR}/redis
#        BUILD_COMMAND     ${CMAKE_COMMAND} --build .
#        INSTALL_COMMAND   ${CMAKE_COMMAND} -E copy_if_different ./redis ${CMAKE_CURRENT_BINARY_DIR}/redis.wasm 
# )
# ExternalProject_Add(redis-native
#        SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/redis
#        UPDATE_COMMAND    ""
#        PATCH_COMMAND     ""
#        CONFIGURE_COMMAND  ${CMAKE_COMMAND}
#        -DCMAKE_BUILD_TYPE=Release
#        -GNinja -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1 -DCMAKE_BUILD_TYPE=Debug
#        ${CMAKE_CURRENT_SOURCE_DIR}/redis
#        BUILD_COMMAND     ${CMAKE_COMMAND} --build .
#        INSTALL_COMMAND   ${CMAKE_COMMAND} -E copy_if_different ./redis ${CMAKE_CURRENT_BINARY_DIR}/redis
# )

ExternalProject_Add(llama
        SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/llama
        UPDATE_COMMAND    ""
        PATCH_COMMAND     ""
        CONFIGURE_COMMAND  ${CMAKE_COMMAND}
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_COMPILER=${WASI_SDK}/bin/clang${WASI_EXE_POSTFIX}
        -DCMAKE_CXX_COMPILER=${WASI_SDK}/bin/clang++${WASI_EXE_POSTFIX}
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--export-all"
       -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
        ${CMAKE_CURRENT_SOURCE_DIR}/llama
        BUILD_COMMAND   ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different ./llama ${CMAKE_CURRENT_BINARY_DIR}/llama.wasm 
)
# ExternalProject_Add(llama-native
#         SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/llama
#         UPDATE_COMMAND    ""
#         PATCH_COMMAND     ""
#         CONFIGURE_COMMAND  ${CMAKE_COMMAND}
#         -GNinja
#         -DCMAKE_BUILD_TYPE=Release
#         ${CMAKE_CURRENT_SOURCE_DIR}/llama
#         BUILD_COMMAND   ${CMAKE_COMMAND} --build .
#         INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different ./llama ${CMAKE_CURRENT_BINARY_DIR}/llama 
# )

ExternalProject_Add(linpack
        SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/linpack
        UPDATE_COMMAND    ""
        PATCH_COMMAND     ""
        CONFIGURE_COMMAND  ${CMAKE_COMMAND}
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_COMPILER=${WASI_SDK}/bin/clang${WASI_EXE_POSTFIX}
        -DCMAKE_CXX_COMPILER=${WASI_SDK}/bin/clang++${WASI_EXE_POSTFIX}
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--export-all"
        -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
        ${CMAKE_CURRENT_SOURCE_DIR}/linpack
        BUILD_COMMAND   ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different ./linpack ${CMAKE_CURRENT_BINARY_DIR}/linpack.wasm
)
ExternalProject_Add(linpack-native
        SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/linpack
        UPDATE_COMMAND    ""
        PATCH_COMMAND     ""
        CONFIGURE_COMMAND  ${CMAKE_COMMAND}
        -GNinja
        -DCMAKE_BUILD_TYPE=Release
        ${CMAKE_CURRENT_SOURCE_DIR}/linpack
        BUILD_COMMAND   ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different ./linpack ${CMAKE_CURRENT_BINARY_DIR}/linpack
)

ExternalProject_Add(hdastar
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/hdastar
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CONFIGURE_COMMAND ${CMAKE_COMMAND}
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_COMPILER=${WASI_SDK}/bin/clang${WASI_EXE_POSTFIX}
        -DCMAKE_CXX_COMPILER=${WASI_SDK}/bin/clang++${WASI_EXE_POSTFIX}
        -DCMAKE_C_FLAGS="-Wl,--allow-undefined-file=${WASI_SDK}/share/wasi-sysroot/share/wasm32-wasi-threads/defined-symbols.txt"
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--export-all"
        -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
        ${CMAKE_CURRENT_SOURCE_DIR}/hdastar
        BUILD_COMMAND ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different ./hw5 ${CMAKE_CURRENT_BINARY_DIR}/hdastar.wasm
)
# ExternalProject_Add(hdastar-native
#         SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/hdastar
#         UPDATE_COMMAND ""
#         PATCH_COMMAND ""
#         CONFIGURE_COMMAND ${CMAKE_COMMAND}
#         -GNinja
#         ${CMAKE_CURRENT_SOURCE_DIR}/hdastar
#         BUILD_COMMAND ${CMAKE_COMMAND} --build .
#         INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different ./hw5 ${CMAKE_CURRENT_BINARY_DIR}/hdastar
# )

if(WAMR_BUILD_AOT)
function(bench_wamr_app input)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)")
        add_custom_target(bench_${input}_compile ALL COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --target=aarch64v8 --cpu=apple-m1 --enable-checkpoint -o ${CMAKE_CURRENT_BINARY_DIR}/${input}.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm)
    else()
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}-pure.aot
          COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --target=x86_64 --cpu=skylake -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-pure.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
          DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_pure_compile ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}-pure.aot)

        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}-stack.aot
          COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --target=x86_64 --cpu=skylake --disable-aux-stack-check --enable-dump-call-stack -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-stack.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
          DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_stack_compile ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}-stack.aot)

        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-restore.aot
          COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --target=x86_64 --cpu=skylake --disable-aux-stack-check --enable-checkpoint --enable-restore -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-restore.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
          DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_compile ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-restore.aot)

        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt.aot
          COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --target=x86_64 --cpu=skylake --disable-aux-stack-check --enable-checkpoint -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
          DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_ckpt_compile ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt.aot)

        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-loop.aot
          COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --target=x86_64 --cpu=skylake --disable-aux-stack-check --enable-loop-checkpoint -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-loop.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
          DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_ckpt_loop_compile ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-loop.aot)

        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-restore-loop.aot
          COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --target=x86_64 --cpu=skylake --disable-aux-stack-check --enable-loop-checkpoint --enable-restore -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-restore-loop.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
          DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_ckpt_restore_loop_compile ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-restore-loop.aot)

        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-br.aot
          COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --target=x86_64 --cpu=skylake --disable-aux-stack-check --enable-br-checkpoint -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-br.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
          DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_ckpt_br_compile ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-br.aot)

        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-restore-br.aot
          COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --target=x86_64 --cpu=skylake --disable-aux-stack-check --enable-br-checkpoint --enable-restore -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-restore-br.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
          DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_ckpt_restore_br_compile ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-restore-br.aot)

        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-every-dirty.aot
          COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --target=x86_64 --cpu=skylake --disable-aux-stack-check --enable-every-checkpoint --enable-aux-stack-dirty-bit -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-every-dirty.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
          DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_ckpt_every_dirty_compile DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-every-dirty.aot)

        # add_custom_target(${input}_compile_all ALL DEPENDS ${input}_pure_compile ${input}_stack_compile ${input}_compile ${input}_ckpt_br_compile)
    endif()
add_test(NAME bench_${input}_checkpoint COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../MVVM_checkpoint --target ${CMAKE_CURRENT_BINARY_DIR}/${input}.aot)
add_test(NAME bench_${input}_restore COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../MVVM_restore --target ${CMAKE_CURRENT_BINARY_DIR}/${input}.aot)
endfunction()
bench_wamr_app(bc)
bench_wamr_app(bfs)
bench_wamr_app(cc)
bench_wamr_app(cc_sv)
bench_wamr_app(converter)
bench_wamr_app(pr)
bench_wamr_app(pr_spmv)
bench_wamr_app(sssp)
bench_wamr_app(tc)
# bench_wamr_app(redis)
bench_wamr_app(llama)
bench_wamr_app(linpack)
endif()
