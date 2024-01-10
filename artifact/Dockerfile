FROM ubuntu:23.04

WORKDIR /

RUN apt-get update
RUN apt install -y wget make gcc-13 g++-13 cmake git libprotobuf-c-dev libprotobuf-dev libgrpc-dev libfmt-dev llvm-14-dev libedit-dev libcxxopts-dev libpfm4-dev libbenchmark-dev ninja-build libpcap-dev libssl-dev

RUN cd /opt && wget http://108.181.27.85/wasi/wasi-sdk-21.0-linux.tar.gz && tar -xzf wasi-sdk-*.tar.gz && mv wasi-sdk-21 wasi-sdk
RUN cd / && git clone https://github.com/Multi-V-VM/MVVM
RUN cd MVVM && git submodule update --init --recursive
RUN cd /MVVM/lib/wasm-micro-runtime/samples/socket-api/ && cmake . && make -j && cd -
RUN cd /MVVM/lib/wasm-micro-runtime/wamr-compiler && mkdir build && cd build &&cmake .. -DWAMR_BUILD_AOT=1 -DWAMR_BUILD_AOT_STACK_FRAME=1 -DWAMR_BUILD_DEBUG_AOT=0 -DWAMR_BUILD_DUMP_CALL_STACK=1 -DWAMR_BUILD_CUSTOM_NAME_SECTION=0 -DWAMR_BUILD_SHARED_MEMORY=1 -DWAMR_BUILD_WITH_CUSTOM_LLVM=1 -DLLVM_DIR=/usr/lib/llvm-14/lib/cmake/llvm/ && make -j && cd -
RUN cd /MVVM && mkdir build && cd build && CC=gcc-13 CXX=g++-13 cmake .. && make

