if (WIN32)
    set(WASI_SDK "D:/wasi-sdk")
    set(EMSDK_DIR "D:/emsdk/upstream")
    set(WASI_EXE_POSTFIX ".exe")
else ()
    set(WASI_SDK "/opt/wasi-sdk")
    set(EMSDK_DIR "/opt/emsdk/upstream")
    set(WASI_EXE_POSTFIX "")
endif ()

include(ExternalProject)

ExternalProject_Add(gapbs
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/gapbs
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CONFIGURE_COMMAND ${CMAKE_COMMAND}
        -DCMAKE_BUILD_TYPE=Release
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        -DCMAKE_TOOLCHAIN_FILE=${WASI_SDK}/share/cmake/wasi-sdk-pthread.cmake
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--export-all -Wl,--allow-undefined-file=${WASI_SDK}/share/wasi-sysroot/share/wasm32-wasi-threads/undefined-symbols.txt"
        -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
        ${CMAKE_CURRENT_SOURCE_DIR}/gapbs
        BUILD_COMMAND ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different ./bc ${CMAKE_CURRENT_BINARY_DIR}/bc.wasm &&
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
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/gapbs
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CONFIGURE_COMMAND ${CMAKE_COMMAND}
        -DCMAKE_BUILD_TYPE=Release
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        ${CMAKE_CURRENT_SOURCE_DIR}/gapbs
        BUILD_COMMAND ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different ./bc ${CMAKE_CURRENT_BINARY_DIR}/bc &&
        ${CMAKE_COMMAND} -E copy_if_different ./bfs ${CMAKE_CURRENT_BINARY_DIR}/bfs &&
        ${CMAKE_COMMAND} -E copy_if_different ./cc ${CMAKE_CURRENT_BINARY_DIR}/cc &&
        ${CMAKE_COMMAND} -E copy_if_different ./cc_sv ${CMAKE_CURRENT_BINARY_DIR}/cc_sv &&
        ${CMAKE_COMMAND} -E copy_if_different ./converter ${CMAKE_CURRENT_BINARY_DIR}/converter &&
        ${CMAKE_COMMAND} -E copy_if_different ./pr ${CMAKE_CURRENT_BINARY_DIR}/pr &&
        ${CMAKE_COMMAND} -E copy_if_different ./pr_spmv ${CMAKE_CURRENT_BINARY_DIR}/pr_spmv &&
        ${CMAKE_COMMAND} -E copy_if_different ./sssp ${CMAKE_CURRENT_BINARY_DIR}/sssp &&
        ${CMAKE_COMMAND} -E copy_if_different ./tc ${CMAKE_CURRENT_BINARY_DIR}/tc
)
 ExternalProject_Add(redis
        SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/redis
        UPDATE_COMMAND    ""
        PATCH_COMMAND     ""
        CONFIGURE_COMMAND  ${CMAKE_COMMAND}
        -DCMAKE_BUILD_TYPE=Release
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        -DCMAKE_TOOLCHAIN_FILE=${WASI_SDK}/share/cmake/wasi-sdk-pthread.cmake
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--export-all -Wl,--allow-undefined-file=${WASI_SDK}/share/wasi-sysroot/share/wasm32-wasi-threads/undefined-symbols.txt"
        -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
        ${CMAKE_CURRENT_SOURCE_DIR}/redis
        BUILD_COMMAND     ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND   ${CMAKE_COMMAND} -E copy_if_different ./redis ${CMAKE_CURRENT_BINARY_DIR}/redis.wasm
 )
 ExternalProject_Add(redis-native
        SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/redis
        UPDATE_COMMAND    ""
        PATCH_COMMAND     ""
        CONFIGURE_COMMAND  ${CMAKE_COMMAND}
        -DCMAKE_BUILD_TYPE=Release
        -GNinja -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1 -DCMAKE_BUILD_TYPE=Debug
        ${CMAKE_CURRENT_SOURCE_DIR}/redis
        BUILD_COMMAND     ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND   ${CMAKE_COMMAND} -E copy_if_different ./redis ${CMAKE_CURRENT_BINARY_DIR}/redis
 )
ExternalProject_Add(ftp
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ftp
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CONFIGURE_COMMAND ${CMAKE_COMMAND}
        -DCMAKE_BUILD_TYPE=Release
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        -DCMAKE_TOOLCHAIN_FILE=${WASI_SDK}/share/cmake/wasi-sdk.cmake
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--export-all -Wl,--allow-undefined-file=${WASI_SDK}/share/wasi-sysroot/share/wasm32-wasi-threads/undefined-symbols.txt"
        -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
        ${CMAKE_CURRENT_SOURCE_DIR}/ftp
        BUILD_COMMAND ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different ./dataServer ${CMAKE_CURRENT_BINARY_DIR}/dataServer.wasm && ${CMAKE_COMMAND} -E copy_if_different ./remoteClient ${CMAKE_CURRENT_BINARY_DIR}/remoteClient.wasm
)
ExternalProject_Add(nas
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/nas
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CONFIGURE_COMMAND ${CMAKE_COMMAND}
        -DCMAKE_BUILD_TYPE=Release
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        -DCMAKE_TOOLCHAIN_FILE=${WASI_SDK}/share/cmake/wasi-sdk.cmake
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--export-all -Wl,--allow-undefined-file=${WASI_SDK}/share/wasi-sysroot/share/wasm32-wasi-threads/undefined-symbols.txt"
        -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
        ${CMAKE_CURRENT_SOURCE_DIR}/nas
        BUILD_COMMAND ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different ./bt ${CMAKE_CURRENT_BINARY_DIR}/bt.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./cg ${CMAKE_CURRENT_BINARY_DIR}/cg.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./ep ${CMAKE_CURRENT_BINARY_DIR}/ep.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./ft ${CMAKE_CURRENT_BINARY_DIR}/ft.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./is ${CMAKE_CURRENT_BINARY_DIR}/is.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./lu ${CMAKE_CURRENT_BINARY_DIR}/lu.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./mg ${CMAKE_CURRENT_BINARY_DIR}/mg.wasm &&
        ${CMAKE_COMMAND} -E copy_if_different ./sp ${CMAKE_CURRENT_BINARY_DIR}/sp.wasm
)
ExternalProject_Add(llama
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/llama
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CONFIGURE_COMMAND ${CMAKE_COMMAND}
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        -DCMAKE_TOOLCHAIN_FILE=${WASI_SDK}/share/cmake/wasi-sdk-pthread.cmake
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--export-all -Wl,--allow-undefined-file=${WASI_SDK}/share/wasi-sysroot/share/wasm32-wasi-threads/undefined-symbols.txt"
        -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
        ${CMAKE_CURRENT_SOURCE_DIR}/llama
        BUILD_COMMAND ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different ./llama ${CMAKE_CURRENT_BINARY_DIR}/llama.wasm
)

ExternalProject_Add(linpack
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/linpack
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CONFIGURE_COMMAND ${CMAKE_COMMAND}
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        -DCMAKE_TOOLCHAIN_FILE=${WASI_SDK}/share/cmake/wasi-sdk.cmake
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--export-all -Wl,--allow-undefined-file=${WASI_SDK}/share/wasi-sysroot/share/wasm32-wasi-threads/undefined-symbols.txt"
        -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
        ${CMAKE_CURRENT_SOURCE_DIR}/linpack
        BUILD_COMMAND ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different ./linpack ${CMAKE_CURRENT_BINARY_DIR}/linpack.wasm
)
ExternalProject_Add(hdastar
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/hdastar
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CONFIGURE_COMMAND ${CMAKE_COMMAND}
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        -DCMAKE_TOOLCHAIN_FILE=${WASI_SDK}/share/cmake/wasi-sdk.cmake
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--export-all -Wl,--allow-undefined-file=${WASI_SDK}/share/wasi-sysroot/share/wasm32-wasi-threads/undefined-symbols.txt"
        -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
        ${CMAKE_CURRENT_SOURCE_DIR}/hdastar
        BUILD_COMMAND ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different ./hw5 ${CMAKE_CURRENT_BINARY_DIR}/hdastar.wasm
)

if (WAMR_BUILD_AOT)
    function(bench_wamr_app input)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}-pure.aot
                COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-pure.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
                DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_pure_compile ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}-pure.aot)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}-stack.aot
                COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --disable-aux-stack-check --enable-dump-call-stack -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-stack.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
                DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_stack_compile ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}-stack.aot)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}.aot
                COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --disable-aux-stack-check --enable-checkpoint -o ${CMAKE_CURRENT_BINARY_DIR}/${input}.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
                DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_compile ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.aot)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-br.aot
                COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --disable-aux-stack-check --enable-br-checkpoint -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-br.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
                DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_ckpt_br_compile ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-br.aot)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-every-dirty.aot
                COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --disable-aux-stack-check --enable-every-checkpoint --enable-aux-stack-dirty-bit -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-every-dirty.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
                DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_ckpt_every_dirty_compile DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-every-dirty.aot)
        add_custom_target(${input}_compile_all ALL DEPENDS ${input}_pure_compile ${input}_stack_compile ${input}_compile ${input}_ckpt_br_compile)
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
    bench_wamr_app(redis)
    bench_wamr_app(remoteClient)
    bench_wamr_app(dataServer)
    bench_wamr_app(bt)
    bench_wamr_app(cg)
    bench_wamr_app(ep)
    bench_wamr_app(ft)
    bench_wamr_app(is)
    bench_wamr_app(lu)
    bench_wamr_app(mg)
    bench_wamr_app(sp)
    bench_wamr_app(linpack)
    bench_wamr_app(llama)
    bench_wamr_app(hdastar)
endif ()
