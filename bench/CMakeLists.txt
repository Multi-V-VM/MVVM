find_package(benchmark REQUIRED)
add_executable(mvvm_benchmark bench.cpp)

# Link benchmark library to your test target
target_link_libraries(mvvm_benchmark benchmark)
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
                    -DCMAKE_TOOLCHAIN_FILE=${WASI_SDK_DIR}/share/cmake/wasi-sdk.cmake
                    -DCMAKE_SYSROOT=${WASI_SDK_DIR}/share/wasi-sysroot
                    ${CMAKE_CURRENT_SOURCE_DIR}/clickhouse
BUILD_COMMAND     ${CMAKE_COMMAND} --build .
)