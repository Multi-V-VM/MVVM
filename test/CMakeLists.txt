if (WIN32)
    set(WASI_SDK_DIR D:/wasi-sdk/)
else ()
    set(WASI_SDK_DIR /opt/wasi-sdk/)
endif ()

set(WAMR_DIR ${PROJECT_SOURCE_DIR}/lib/wasm-micro-runtime/)
set(HOST_PREFIX ${WASI_SDK_DIR}/bin/)
enable_testing()
function(wamr_openmp_app input)
      add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        COMMAND ${HOST_PREFIX}clang -fopenmp=libomp -lomp -Wno-implicit-function-declaration -O3 -Wno-int-conversion --target=wasm32-wasi-threads -lwasi-emulated-getpid  -lwasi-emulated-process-clocks -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS -g -pthread -Wl,--max-memory=67108864 -z stack-size=65536 -Wl,--export-all -Wl,--allow-undefined -Wl,--allow-undefined-file=${WASI_SDK_DIR}/share/wasi-sysroot/share/wasm32-wasi-threads/undefined-symbols.txt --sysroot=${HOST_PREFIX}/../share/wasi-sysroot ${CMAKE_CURRENT_SOURCE_DIR}/${input}.c -o ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${input}.c
      )
    add_custom_target(${input}_wamr ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm)
    if (WAMR_BUILD_AOT)
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
        add_test(NAME ${input}_checkpoint COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../MVVM_checkpoint --target ${CMAKE_CURRENT_BINARY_DIR}/${input}.aot)
        add_test(NAME ${input}_restore COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../MVVM_restore --target ${CMAKE_CURRENT_BINARY_DIR}/${input}.aot)
    else ()
        add_test(NAME ${input}_checkpoint COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../MVVM_checkpoint --target ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm)
        add_test(NAME ${input}_restore COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../MVVM_restore --target ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm)
endif ()
endfunction()
function(wamr_app input)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
            COMMAND ${HOST_PREFIX}clang -Wno-implicit-function-declaration -Wno-int-conversion --target=wasm32-unknown-wasi -g -pthread -z stack-size=65536 -Wl,--initial-memory=655360 -Wl,--max-memory=655360 -Wl,--export-all -Wl,--allow-undefined -Wl,--allow-undefined-file=${WAMR_DIR}/wamr-sdk/app/libc-builtin-sysroot/share/defined-symbols.txt --sysroot=${HOST_PREFIX}/../share/wasi-sysroot ${CMAKE_CURRENT_SOURCE_DIR}/${input}.c -o ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${input}.c
    )
    add_custom_target(${input}_wamr ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm)
    if (WAMR_BUILD_AOT)
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

        add_test(NAME ${input}_checkpoint COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../MVVM_checkpoint --target ${CMAKE_CURRENT_BINARY_DIR}/${input}.aot)
        add_test(NAME ${input}_restore COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../MVVM_restore --target ${CMAKE_CURRENT_BINARY_DIR}/${input}.aot)
    else ()
        add_test(NAME ${input}_checkpoint COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../MVVM_checkpoint --target ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm)
        add_test(NAME ${input}_restore COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../MVVM_restore --target ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm)
    endif ()
endfunction()

wamr_app(counter)
wamr_app(multi-thread)
wamr_app(mutex)
wamr_app(read-file)
wamr_app(gups)
wamr_app(gemm)
wamr_openmp_app(openmp)

function(wamr_socket_app input)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
            COMMAND ${HOST_PREFIX}clang --target=wasm32-unknown-wasi -g -pthread -Wl,--export-all -Wl,--allow-undefined-file=${WAMR_DIR}/wamr-sdk/app/libc-builtin-sysroot/share/defined-symbols.txt --sysroot=${HOST_PREFIX}/../share/wasi-sysroot ${WAMR_DIR}/samples/socket-api/wasm-app-prefix/src/wasm-app-build/libsocket_wasi_ext.a -I${WAMR_DIR}/core/iwasm/libraries/lib-socket/inc/ ${CMAKE_CURRENT_SOURCE_DIR}/${input}.c -o ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${input}.c
    )
    add_custom_target(${input}_wamr ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm)
    if (WAMR_BUILD_AOT)
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

        # add_custom_target(${input}_ckpt_br_compile ALL DEPENDS ${input}_wamr COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --disable-aux-stack-check --enable-br-checkpoint -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-br.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-br.aot
                COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --disable-aux-stack-check --enable-br-checkpoint -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-br.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
                DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm
        )
        add_custom_target(${input}_ckpt_br_compile ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-br.aot)

        # add_custom_target(${input}_ckpt_every_dirty_compile ALL DEPENDS ${input}_wamr COMMAND ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/wamr-compiler/build/wamrc --disable-aux-stack-check --enable-every-checkpoint --enable-aux-stack-dirty-bit -o ${CMAKE_CURRENT_BINARY_DIR}/${input}-ckpt-every-dirty.aot ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm)

        add_custom_target(${input}_compile_all ALL DEPENDS ${input}_pure_compile ${input}_stack_compile ${input}_compile ${input}_ckpt_br_compile)

        add_test(NAME ${input}_checkpoint COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../MVVM_checkpoint --target ${CMAKE_CURRENT_BINARY_DIR}/${input}.aot)
        # add_test(NAME ${input}_restore COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../MVVM_restore --target ${CMAKE_CURRENT_BINARY_DIR}/${input}.aot)
    else ()
        add_test(NAME ${input}_checkpoint COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../MVVM_checkpoint --target ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm)
        add_test(NAME ${input}_restore COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../MVVM_restore --target ${CMAKE_CURRENT_BINARY_DIR}/${input}.wasm)
    endif ()
endfunction()

wamr_socket_app(server)
wamr_socket_app(client)
wamr_socket_app(tcp_server)
wamr_socket_app(tcp_client)