#!/usr/bin/env bash
set -euo pipefail
# Usage: WAMRC=... test/build_dwarf_benchmarks.sh OUTPUT COREMARK_SOURCE
out=$(realpath -m "$1")
coremark=$(realpath "$2")
root=$(cd "$(dirname "$0")/.." && pwd)
: "${WAMRC:?set WAMRC to the patched WAMR 2.4.5 compiler}"
mkdir -p "$out"
python3 "$root/test/coremark_dwarf/prepare.py" "$coremark" "$out/coremark"
clang-20 --target=wasm32 -mno-reference-types -O2 -nostdlib -Wl,--no-entry -Wl,--export=run -Wl,--export=effects -Wl,--allow-undefined "$root/test/dwarf_resume_workload.c" -o "$out/indirect.wasm"
clang-20 --target=wasm32 -mno-reference-types -O2 -fno-builtin -nostdlib -I"$out/coremark" -Wl,--no-entry -Wl,--export=run -Wl,--export=effects -Wl,--allow-undefined "$out/coremark/driver.c" "$out/coremark/core_list_join.c" "$out/coremark/core_matrix.c" "$out/coremark/core_state.c" "$out/coremark/core_util.c" -o "$out/coremark.wasm"
common=(--size-level=3 --disable-simd --bounds-checks=0 --stack-bounds-checks=0 --disable-aux-stack-check)
for arch in riscv64 x86_64; do
  target=(--target="$arch")
  llc_args=()
  if [[ $arch == riscv64 ]]; then
    target+=(--cpu=generic-rv64 --cpu-features=+m,+a,+f,+d,+c)
    llc_args+=(-mattr=+m,+a,+f,+d,+c)
  else
    target+=(--cpu=x86-64)
  fi
  for name in indirect coremark; do
    base="$out/$name.$arch"
    "$WAMRC" "${target[@]}" "${common[@]}" --opt-level=3 -o "$base.aot" "$out/$name.wasm"
    "$WAMRC" "${target[@]}" "${common[@]}" --opt-level=4 --enable-segue -o "$base.o4-segue.aot" "$out/$name.wasm"
    "$WAMRC" "${target[@]}" "${common[@]}" --opt-level=3 --format=llvmir-unopt --mllvm=-mvvm-dwarf-resume -o "$base.ll" "$out/$name.wasm"
    llc-20 -O3 -filetype=obj -relocation-model=pic -generate-arange-section "${llc_args[@]}" "$base.ll" -o "$base.o"
    if [[ $(uname -m) == "$arch" ]]; then
      gcc-16 -shared "$base.o" -o "$base.so"
    fi
  done
done
git -C "$coremark" rev-parse HEAD > "$out/coremark-source-commit.txt"
sha256sum "$out"/*.wasm "$out"/*.aot "$out"/*.ll "$out"/*.o > "$out/SHA256SUMS"
