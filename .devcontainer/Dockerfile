FROM ubuntu:lunar

RUN apt-get update && apt-get install -y \
    libssl-dev gcc-13 g++-13 libprotobuf-c-dev libprotobuf-dev libgrpc-dev libspdlog-dev libfmt-dev llvm-14-dev libedit-dev libcxxopts-dev libpfm4-dev liblldb-dev libbenchmark-dev ninja-build libpcap-dev libopenblas-pthread-dev \
    librdmacm-dev libibverbs-dev \
    wget git cmake ninja-build python3 python3-pip \
    lsb-release software-properties-common gnupg

RUN apt-get install -y python3-ipykernel python3-numpy python3-matplotlib

RUN cd /opt \
    && wget http://108.181.27.85/wasi/wasi-sdk-21.0-linux.tar.gz \
    && tar -xzf wasi-sdk-*.tar.gz \
    && mv wasi-sdk-21 wasi-sdk \
    && rm wasi-sdk-21.0-linux.tar.gz

RUN wget -4 -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add - \
    && add-apt-repository -y "deb http://apt.llvm.org/lunar/ llvm-toolchain-lunar-17 main" \
    && apt-get update \
    && apt-get install -y clang-17 lldb-17 lld-17 clangd-17 \
    tmux vim gdb wabt neovim htop

USER ubuntu
