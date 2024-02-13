add_executable(gateway ${SOURCE_FILES} main.cpp)
target_link_libraries(gateway pcap spdlog::spdlog fmt::fmt crafter resolv MVVM_export)
add_executable(playground ${SOURCE_FILES} playground.cpp)
target_link_libraries(playground pcap crafter resolv)
