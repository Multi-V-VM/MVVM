find_package(benchmark REQUIRED)
add_executable(mvvm_benchmark bench.cpp)

# Link benchmark library to your test target
target_link_libraries(mvvm_benchmark benchmark)
# add_subdirectory(gapbs)