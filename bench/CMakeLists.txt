add_executable(mvvm_benchmark bench.cpp)
target_link_libraries(mvvm_benchmark benchmark::benchmark)
if(WIN32)
    set(WASI_SDK_DIR D:/wasi-sdk/)
    set(EMSDK_DIR D:/emsdk/)
else()
    set(WASI_SDK_DIR /opt/wasi-sdk/)
    set(EMSDK_DIR /opt/emsdk/)
endif()
include(ExternalProject)

ExternalProject_Add(gapbs
SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/gapbs
UPDATE_COMMAND    ""
PATCH_COMMAND     ""
CONFIGURE_COMMAND  ${CMAKE_COMMAND}
                    -DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_CXX_COMPILER_WORKS=1
                    -GNinja
                    -DWASI_SDK_DIR=${WASI_SDK_DIR}
                    -DCMAKE_TOOLCHAIN_FILE=${WASI_SDK_DIR}/share/cmake/wasi-sdk.cmake
                    -DCMAKE_SYSROOT=${WASI_SDK_DIR}/share/wasi-sysroot
                    ${CMAKE_CURRENT_SOURCE_DIR}/gapbs
BUILD_COMMAND     ${CMAKE_COMMAND} --build .
)

ExternalProject_Add(clickhouse
SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/clickhouse
UPDATE_COMMAND    ""
PATCH_COMMAND     ""
CONFIGURE_COMMAND  ${CMAKE_COMMAND}
                    -DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_CXX_COMPILER_WORKS=1
                    -GNinja
                    -DWASI_SDK_DIR=${WASI_SDK_DIR}
                    -DCMAKE_TOOLCHAIN_FILE=${EMSDK_DIR}/share/cmake/wasi-sdk.cmake
                    -DCMAKE_SYSROOT=${WASI_SDK_DIR}/share/wasi-sysroot -DEM
                    ${CMAKE_CURRENT_SOURCE_DIR}/clickhouse
BUILD_COMMAND     ${CMAKE_COMMAND} --build .
)

ExternalProject_Add(llama
        SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/llama
        UPDATE_COMMAND    ""
        PATCH_COMMAND     ""
        CONFIGURE_COMMAND  ${CMAKE_COMMAND}
        -DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_CXX_COMPILER_WORKS=1
        -GNinja
        -DWASI_SDK_DIR=${WASI_SDK_DIR}
        -DCMAKE_TOOLCHAIN_FILE=${WASI_SDK_DIR}/share/cmake/wasi-sdk.cmake
        -DCMAKE_SYSROOT=${WASI_SDK_DIR}/share/wasi-sysroot
        ${CMAKE_CURRENT_SOURCE_DIR}/llama
        BUILD_COMMAND     ${CMAKE_COMMAND} --build .
)