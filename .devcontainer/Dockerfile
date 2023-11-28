FROM ubuntu:lunar

RUN apt-get update && apt-get install -y \
    libssl-dev gcc-13 g++-13 libprotobuf-c-dev libprotobuf-dev libgrpc-dev libfmt-dev llvm-14-dev libedit-dev libcxxopts-dev libpfm4-dev liblldb-dev libbenchmark-dev ninja-build libpcap-dev \
    wget git cmake ninja-build python3 python3-pip \
    lsb-release software-properties-common gnupg

RUN cd /opt \
    && wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-20/wasi-sdk-20.0-linux.tar.gz \
    && tar -xzf wasi-sdk-*.tar.gz \
    && mv wasi-sdk-20.0 wasi-sdk \
    && rm wasi-sdk-20.0-linux.tar.gz

RUN mv /opt/wasi-sdk/bin/wasm-ld /opt/wasi-sdk/bin/wasm-ld.bak \
    && echo '#!/bin/bash \n/opt/wasi-sdk/bin/wasm-ld.bak --no-check-features "$@"' | tee /opt/wasi-sdk/bin/wasm-ld \
    && chmod +x /opt/wasi-sdk/bin/wasm-ld

RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add - \
    && add-apt-repository -y "deb http://apt.llvm.org/lunar/ llvm-toolchain-lunar-17 main" \
    && apt-get update \
    && apt-get install -y clang-17 lldb-17 lld-17 clangd-17 \
    tmux vim gdb wabt

USER ubuntu
