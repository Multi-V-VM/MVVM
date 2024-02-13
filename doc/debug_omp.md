# MVVM Integration with OpenMP and WASI SDK

This design document outlines the process for integrating OpenMP code within an MVVM architecture, leveraging the WASI SDK for compilation and execution. The steps include setting up the environment, compiling the OpenMP code, and running tests to verify the integration.

## Setup and Compilation

1. **Code Location**: The OpenMP code is primarily located at:

```bash
/mnt/wasm-openmp-examples/llvm-project/openmp/runtime/src/kmp_runtime.cpp
```

2. **Compilation Preparation**:

   - Navigate to the folder containing the OpenMP examples:

```bash
cd /mnt/wasm-openmp-examples
```

   - Compile the project and copy the resulting libraries to the WASI SDK system root:

  ```bash
     make
cp build/runtime/src/lib* /opt/wasi-sdk/share/wasi-sysroot/lib/wasm32-wasi-threads/
```

## MVVM Build Process

1. **Building MVVM**:

   - Clean and prepare the MVVM build directory:

  ```bash
     cd /mnt/MVVM/build
rm -rf bench/nas-prefix/ && ninja nas && ninja mg_compile
```

   - Execute MVVM-specific build commands:

```
     Copy code
ninja MVVM_checkpoint MVVM_restore
```

2. **Testing**:

   - Run the MVVM checkpoint with specific arguments and environment variables:

  ```bash
LOGV=1 ../build/MVVM_checkpoint -t bench/mg.aot -a -g12,-n1000 -e OMP_NUM_THREADS=2 -e KMP_DEBUG=511
```

## Additional Notes

- Ensure that all paths and environment variables are correctly set according to your system's configuration.
- The testing step involves setting the number of OpenMP threads (`OMP_NUM_THREADS=2`) and enabling verbose debugging (`KMP_DEBUG=511`) to facilitate troubleshooting and optimization.
