cmake_minimum_required(VERSION 3.10)
project(MVVM CXX)
set(CMAKE_CXX_STANDARD 23)
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
# grpc_generate_cpp(GRPC_SRCS GRPC_HDRS ${PROTO_SRC_DIR} ${PROTOS})
include_directories(lib/wasm-micro-runtime/core/iwasm/interpreter)
include_directories(lib/wasm-micro-runtime/core/shared/utils/)
set(WAMR_BUILD_JIT 1)
if(APPLE)
include_directories(lib/wasm-micro-runtime/core/shared/platform/darwin)
else()
include_directories(lib/wasm-micro-runtime/core/shared/platform/linux)
endif()
include(cmake/common.cmake)
# Proto file
function(add_grpc_proto prefix)
    get_filename_component(hw_proto "src/proto/${prefix}.proto" ABSOLUTE)
    get_filename_component(hw_proto_path "${hw_proto}" PATH)

    # Generated sources
    set(hw_proto_srcs ${CMAKE_CURRENT_BINARY_DIR}/${prefix}.pb.cc ${hw_proto_srcs} PARENT_SCOPE)
    set(hw_proto_hdrs ${CMAKE_CURRENT_BINARY_DIR}/${prefix}.pb.h ${hw_proto_hdrs} PARENT_SCOPE)
    set(hw_grpc_srcs ${CMAKE_CURRENT_BINARY_DIR}/${prefix}.grpc.pb.cc ${hw_grpc_srcs} PARENT_SCOPE)
    set(hw_grpc_hdrs ${CMAKE_CURRENT_BINARY_DIR}/${prefix}.grpc.pb.h ${hw_grpc_hdrs} PARENT_SCOPE)
    add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${prefix}.pb.cc" "${CMAKE_CURRENT_BINARY_DIR}/${prefix}.pb.h" "${CMAKE_CURRENT_BINARY_DIR}/${prefix}.grpc.pb.cc" "${CMAKE_CURRENT_BINARY_DIR}/${prefix}.grpc.pb.h"
            COMMAND ${_PROTOBUF_PROTOC}
            ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
            --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
            -I "${hw_proto_path}"
            --plugin=protoc-gen-grpc= "${_GRPC_CPP_PLUGIN_EXECUTABLE}"
            "${hw_proto}"
            DEPENDS "${hw_proto}")
endfunction()

# Include generated *.pb.h files
include_directories("${CMAKE_CURRENT_BINARY_DIR}")
add_grpc_proto(wasm_module_instance)
add_grpc_proto(wasm_memory_instance)
add_grpc_proto(wamr)
# hw_grpc_proto
add_library(hw_grpc_proto
        ${hw_grpc_srcs}
        ${hw_grpc_hdrs}
        ${hw_proto_srcs}
        ${hw_proto_hdrs})
target_link_libraries(hw_grpc_proto
        ${_REFLECTION}
        ${_GRPC_GRPCPP}
        ${_PROTOBUF_LIBPROTOBUF})

# Targets greeter_[async_](client|server)
foreach (_target
        recv send)
    add_executable(MVVM_${_target} "src/${_target}.cpp")
    target_link_libraries(MVVM_${_target}
            hw_grpc_proto
            ${_REFLECTION}
            ${_GRPC_GRPCPP}
            ${_PROTOBUF_LIBPROTOBUF})
endforeach ()