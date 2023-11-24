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
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        -DCMAKE_C_COMPILER=${WASI_SDK}/bin/clang${WASI_EXE_POSTFIX}
        -DCMAKE_CXX_COMPILER=${WASI_SDK}/bin/clang++${WASI_EXE_POSTFIX}
        -DCMAKE_CXX_FLAGS="-fno-exceptions"
        -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
        ${CMAKE_CURRENT_SOURCE_DIR}/gapbs
        BUILD_COMMAND     ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND   ""
)
ExternalProject_Add(redis
       SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/redis
       UPDATE_COMMAND    ""
       PATCH_COMMAND     ""
       CONFIGURE_COMMAND  ${CMAKE_COMMAND}
       -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
       -DCMAKE_C_COMPILER=${WASI_SDK}/bin/clang${WASI_EXE_POSTFIX}
       -DCMAKE_CXX_COMPILER=${WASI_SDK}/bin/clang++${WASI_EXE_POSTFIX} 
       -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
       ${CMAKE_CURRENT_SOURCE_DIR}/redis
       BUILD_COMMAND     ${CMAKE_COMMAND} --build .
       INSTALL_COMMAND   ""
)

ExternalProject_Add(llama
        SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/llama
        UPDATE_COMMAND    ""
        PATCH_COMMAND     ""
        CONFIGURE_COMMAND  ${CMAKE_COMMAND}
        -GNinja -DWASI_SDK_DIR=${WASI_SDK} -DWASI_SDK_PREFIX=${WASI_SDK}
        -DCMAKE_C_COMPILER=${WASI_SDK}/bin/clang${WASI_EXE_POSTFIX}
        -DCMAKE_CXX_COMPILER=${WASI_SDK}/bin/clang++${WASI_EXE_POSTFIX}
        -DCMAKE_SYSROOT=${WASI_SDK}/share/wasi-sysroot -DCMAKE_C_COMPILER_WORK=1 -DCMAKE_CXX_COMPILER_WORK=1
        ${CMAKE_CURRENT_SOURCE_DIR}/llama
        BUILD_COMMAND     ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND   ""
)

if(WAMR_BUILD_AOT)
# TODO: build the corresponding aot 
endif()