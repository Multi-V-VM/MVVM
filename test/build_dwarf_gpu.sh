#!/usr/bin/env bash
set -euo pipefail
# WAMRC=... GPU_KERNEL=...i32x4_add.wasm test/build_dwarf_gpu.sh OUTPUT
root=$(cd "$(dirname "$0")/.." && pwd)
out=$(realpath -m "$1")
: "${WAMRC:?patched compiler required}" "${GPU_KERNEL:?checked i32x4.add Wasm required}"
mkdir -p "$out"
python3 - "$GPU_KERNEL" "$out/gpu_kernel.h" <<'PY'
import sys
b=open(sys.argv[1],'rb').read()
open(sys.argv[2],'w').write('static const unsigned char gpu_kernel[]={'+','.join(map(str,b))+'};\n')
PY
clang-20 --target=wasm32 -mno-reference-types -O2 -fno-builtin -nostdlib -I"$out" -DGPU_STATE_BYTES="${GPU_STATE_BYTES:-1048576}" -Wl,--no-entry -Wl,--export=run -Wl,--export=effects -Wl,--allow-undefined "$root/test/dwarf_gpu_workload.c" -o "$out/gpu.wasm"
common=(--target=x86_64 --cpu=x86-64 --size-level=3 --disable-simd --bounds-checks=0 --stack-bounds-checks=0 --disable-aux-stack-check --opt-level=3)
"$WAMRC" "${common[@]}" -o "$out/gpu.aot" "$out/gpu.wasm"
"$WAMRC" "${common[@]}" --format=llvmir-opt --mllvm=-mvvm-dwarf-resume --mllvm=-mvvm-dwarf-optimize -o "$out/gpu.ll" "$out/gpu.wasm"
llc-20 -O3 -filetype=obj -relocation-model=pic -generate-arange-section "$out/gpu.ll" -o "$out/gpu.o"
gcc-16 -shared "$out/gpu.o" -o "$out/gpu.so"
sha256sum "$out"/gpu.{wasm,aot,ll,o,so} > "$out/SHA256SUMS"
