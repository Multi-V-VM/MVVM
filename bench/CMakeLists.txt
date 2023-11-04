add_executable(MVVM_benchmark bench.cpp)
target_link_libraries(MVVM_benchmark benchmark::benchmark)

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
                    -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
                    -DCMAKE_C_COMPILER=${WASI_SDK}/bin/clang${WASI_EXE_POSTFIX}
                    -DCMAKE_CXX_COMPILER=${WASI_SDK}/bin/clang++${WASI_EXE_POSTFIX}
                    -DCMAKE_CXX_FLAGS="-fno-exceptions"
                    -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot
                    ${CMAKE_CURRENT_SOURCE_DIR}/gapbs
BUILD_COMMAND     ${CMAKE_COMMAND} --build .
)
if(NOT WIN32)
    ExternalProject_Add(clickhouse
    SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/clickhouse
    UPDATE_COMMAND    ""
    PATCH_COMMAND     ""
    CONFIGURE_COMMAND  ${CMAKE_COMMAND}
                        -GNinja
                        -DCMAKE_C_COMPILER=${EMSDK_DIR}/bin/clang${WASI_EXE_POSTFIX}
                        -DCMAKE_CXX_COMPILER=${EMSDK_DIR}/bin/clang++${WASI_EXE_POSTFIX}
                        -DCMAKE_SYSROOT=${EMSDK_DIR}/emscripten/cache/sysroot -DEMSCRIPTEN=1
                        ${CMAKE_CURRENT_SOURCE_DIR}/clickhouse
    BUILD_COMMAND     ${CMAKE_COMMAND} --build .
    )

    ExternalProject_Add(llama
            SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/llama
            UPDATE_COMMAND    ""
            PATCH_COMMAND     ""
            CONFIGURE_COMMAND  ${CMAKE_COMMAND}
                                 -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
                                 -DCMAKE_C_COMPILER=${WASI_SDK}/bin/clang${WASI_EXE_POSTFIX}
                                 -DCMAKE_CXX_COMPILER=${WASI_SDK}/bin/clang++${WASI_EXE_POSTFIX}
                                 -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot
                                 ${CMAKE_CURRENT_SOURCE_DIR}/llama
            BUILD_COMMAND     ${CMAKE_COMMAND} --build .
    )
endif()