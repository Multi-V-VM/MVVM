# Examples CMakeLists.txt

# Set common include directories for examples
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/core/iwasm/include
    ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/core/iwasm/libraries/wasi-nn/include
    ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/core/shared/utils
    ${CMAKE_SOURCE_DIR}/lib/wasm-micro-runtime/core/shared/platform/include
    ${CMAKE_SOURCE_DIR}/lib/yalantinglibs/include
    ${LLVM_INCLUDE_DIRS}
    ${s2n_SOURCE_DIR}/api
)

# Common libraries for examples
set(EXAMPLE_COMMON_LIBS
    -Wl,--whole-archive
    MVVM_export
    -Wl,--no-whole-archive
    vmlib
    s2n
    tensorflow-lite
    fmt::fmt
    spdlog::spdlog
    jsoncpp_lib  # Add JSON library
    ${LLVM_LIBRARIES}
    LLVM
    ${BLAS_LIBRARIES}
    ${OPENSSL_CRYPTO_LIBRARY}
    ${OPENSSL_SSL_LIBRARY}
    ZLIB::ZLIB
    ${ZSTD_LIBRARIES}
    dl
    pthread
)

# Function to add an example executable
function(add_mvvm_example name source_file)
    add_executable(${name} ${source_file})
    target_link_libraries(${name} ${EXAMPLE_COMMON_LIBS})
    
    # Add cxxopts for C++ examples that might need command line parsing
    if(${source_file} MATCHES "\\.cpp$")
        target_link_libraries(${name} cxxopts::cxxopts)
    endif()
    
    # Set output directory
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
    )
endfunction()

# Add C++ examples
add_mvvm_example(mvvm_demo mvvm_demo.cpp)
add_mvvm_example(financial_trading_agent financial_trading_agent.cpp)
add_mvvm_example(healthcare_diagnostic_agent healthcare_diagnostic_agent.cpp)
add_mvvm_example(iot_edge_agent iot_edge_agent.cpp)
add_mvvm_example(gpu_cc_tflite_demo gpu_cc_tflite_demo.cpp)

# WASI-NN examples need to be compiled to WASM, not native
# They are provided as source code examples to be compiled with wasi-sdk
# Uncomment below if you want to try building them as native apps for testing
# (note: they won't work properly as they use WASI-NN APIs)
#
# add_executable(wasi_nn_mvvm_demo wasi_nn_mvvm_demo.c ${UNCOMMON_SHARED_SOURCE})
# target_link_libraries(wasi_nn_mvvm_demo ${EXAMPLE_COMMON_LIBS})
# set_target_properties(wasi_nn_mvvm_demo PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
# )
#
# add_executable(wasi_nn_tflite_gpu_cc_demo wasi_nn_tflite_gpu_cc_demo.c ${UNCOMMON_SHARED_SOURCE})
# target_link_libraries(wasi_nn_tflite_gpu_cc_demo ${EXAMPLE_COMMON_LIBS})
# set_target_properties(wasi_nn_tflite_gpu_cc_demo PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
# )

# Group examples in IDE
set_property(TARGET mvvm_demo PROPERTY FOLDER "Examples")
set_property(TARGET financial_trading_agent PROPERTY FOLDER "Examples")
set_property(TARGET healthcare_diagnostic_agent PROPERTY FOLDER "Examples")
set_property(TARGET iot_edge_agent PROPERTY FOLDER "Examples")
set_property(TARGET gpu_cc_tflite_demo PROPERTY FOLDER "Examples")
# set_property(TARGET wasi_nn_mvvm_demo PROPERTY FOLDER "Examples")
# set_property(TARGET wasi_nn_tflite_gpu_cc_demo PROPERTY FOLDER "Examples")

# Add custom target to build all examples
add_custom_target(examples
    DEPENDS
        mvvm_demo
        financial_trading_agent
        healthcare_diagnostic_agent
        iot_edge_agent
        gpu_cc_tflite_demo
        # wasi_nn_mvvm_demo        # Needs to be compiled to WASM
        # wasi_nn_tflite_gpu_cc_demo # Needs to be compiled to WASM
)

# Install examples (optional)
install(TARGETS
    mvvm_demo
    financial_trading_agent
    healthcare_diagnostic_agent
    iot_edge_agent
    gpu_cc_tflite_demo
    # wasi_nn_mvvm_demo        # Needs to be compiled to WASM
    # wasi_nn_tflite_gpu_cc_demo # Needs to be compiled to WASM
    RUNTIME DESTINATION bin/examples
)

message(STATUS "Configured MVVM examples:")
message(STATUS "  - mvvm_demo")
message(STATUS "  - financial_trading_agent")
message(STATUS "  - healthcare_diagnostic_agent")
message(STATUS "  - iot_edge_agent")
message(STATUS "  - gpu_cc_tflite_demo")
message(STATUS "  - wasi_nn_mvvm_demo (source only - compile to WASM)")
message(STATUS "  - wasi_nn_tflite_gpu_cc_demo (source only - compile to WASM)")