add_executable(gateway ${SOURCE_FILES} main.cpp)
target_link_libraries(gateway pcap fmt::fmt crafter resolv MVVM_export)