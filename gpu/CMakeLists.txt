find_program(MVVM_GPU_CARGO cargo HINTS "$ENV{HOME}/.cargo/bin" REQUIRED)
set(_rust_root "${PROJECT_SOURCE_DIR}/rust/tensor-wasm-jit")
set(_rust_target "${CMAKE_CURRENT_BINARY_DIR}/rust-target")
set(_rust_lib "${_rust_target}/release/libtensor_wasm_jit.a")
file(GLOB_RECURSE _rust_sources CONFIGURE_DEPENDS "${_rust_root}/src/*.rs")
add_custom_command(OUTPUT "${_rust_lib}"
    COMMAND "${MVVM_GPU_CARGO}" build --locked --release --features cuda-oxide-backend
        --manifest-path "${_rust_root}/Cargo.toml" --target-dir "${_rust_target}"
    DEPENDS ${_rust_sources} "${_rust_root}/Cargo.toml" "${_rust_root}/Cargo.lock"
    VERBATIM)
add_custom_target(mvvm_gpu_tensor_build DEPENDS "${_rust_lib}")
add_library(mvvm_gpu_tensor_jit STATIC IMPORTED)
set_target_properties(mvvm_gpu_tensor_jit PROPERTIES IMPORTED_LOCATION "${_rust_lib}")
add_dependencies(mvvm_gpu_tensor_jit mvvm_gpu_tensor_build)
add_library(mvvm_nvidia_cc STATIC ../src/wamr_gpu_cc_nvidia.cpp ../src/wamr_wasm_to_gpu_translator.cpp)
target_include_directories(mvvm_nvidia_cc PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(mvvm_nvidia_cc PRIVATE MVVM_ENABLE_NVIDIA_CC=1 MVVM_ENABLE_TENSOR_WASM_JIT=1)
target_link_libraries(mvvm_nvidia_cc PUBLIC CUDA::cuda_driver ${MVVM_NVML_LIBRARY} OpenSSL::Crypto mvvm_gpu_tensor_jit ${CMAKE_DL_LIBS} pthread)
add_executable(nvidia_gpu_cc_test ../test/nvidia_gpu_cc_test.cpp)
target_link_libraries(nvidia_gpu_cc_test PRIVATE mvvm_nvidia_cc)
# This test needs a physical CC-On GPU, a confidential guest and a real verifier.
# It is invoked explicitly by the documented hardware procedure, never skipped
# into success on CPU-only CI runners.
