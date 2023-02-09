cmake_minimum_required(VERSION 3.10)
project(MVVM CXX)

set(CMAKE_CXX_STANDARD 23)

set(ASMJIT_STATIC true)
add_subdirectory(lib/elf)
add_subdirectory(lib/riscv-isa)
add_subdirectory(lib/asmjit)

include_directories(include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fno-exceptions -fno-rtti")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb -g3 -fno-omit-frame-pointer")
set(CMAKE_ASM_FLAGS "${CFLAGS} -x assembler-with-cpp")
file(GLOB_RECURSE SOURCE_FILES src/*/*.cpp src/*.cpp)

add_executable(MVVM src ${SOURCE_FILES} src/frontend/jit_frontend.cpp include/frontend/jit_frontend.h)
target_compile_definitions(MVVM PRIVATE
        __RV_BASE_I__ __RV_BIT_WIDTH__=64
        __RV_SUPERVISOR_MODE__ __RV_USER_MODE__
        __RV_EXTENSION_M__ __RV_EXTENSION_A__ __RV_EXTENSION_ZICSR__)
set_target_properties(MVVM PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(MVVM riscv_isa_rv64ima)
