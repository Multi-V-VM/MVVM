set(HOST_PREFIX /opt/wasi-sdk/bin/)
set(WAMR_DIR ${PROJECT_SOURCE_DIR}/lib/wasm-micro-runtime/)
function(wamr_app input)
  add_custom_target(${input}_wamr ALL COMMAND ${HOST_PREFIX}clang --target=wasm32-unknown-wasi -O3 -z stack-size=4096 -Wl,--initial-memory=65536 --sysroot=${HOST_PREFIX}/../share/wasi-sysroot  -Wl,--strip-all,--no-entry -nostdlib ${PROJECT_SOURCE_DIR}/test/${input}.c -o ${PROJECT_BINARY_DIR}/test/${input}.wasm)
endfunction()

wamr_app(counter)