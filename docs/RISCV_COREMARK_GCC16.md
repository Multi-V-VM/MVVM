# RISC-V CoreMark with GCC 16 and WAMR 2.4.5

Validation host: `lanxin2`, `riscv64`, ISA `rv64imafdc...`, LLVM CPU model
`spacemit-x60`. The run used 400,000 iterations and every reported result
passed the CoreMark CRC checks.

## Toolchain

- Native compiler: GCC 16.0.1 20260315 from `ppa:ubuntu-toolchain-r/test`
- Wasm compiler: Ubuntu Clang 20.1.2, `wasm32-wasi`, `-O3`
- WAMR runtime and `wamrc`: WAMR 2.4.5, host-built with GCC/G++ 16
- AOT target features: `+m,+a,+f,+d,+c`

## Results

| Mode | Iterations/s | Total time | Wall time | Max RSS |
| --- | ---: | ---: | ---: | ---: |
| Native GCC 16 `-O3` | 16,124.480993 | 24.807 s | 24.80 s | 908 KiB |
| WAMR AOT, O3, explicit no-SFI | 7,254.788160 | 55.136 s | 55.14 s | 9,804 KiB |

The validated AOT result is 44.99% of the native CoreMark throughput on this
host. This compares different front ends (GCC native versus Clang WASI plus
WAMR AOT), so it is an end-to-end runtime result rather than a compiler-only
comparison.

## AOT command

```sh
wamrc --target=riscv64 \
  --cpu=generic-rv64 \
  --cpu-features=+m,+a,+f,+d,+c \
  --opt-level=3 --size-level=3 \
  --bounds-checks=0 --stack-bounds-checks=0 \
  -o coremark-o3-small-nosfi.aot coremark.wasm
iwasm coremark-o3-small-nosfi.aot
```

## Optimization findings

- `--opt-level=4` is accepted but clamped to level 3. Its AOT file was
  byte-identical to the explicit O3/no-SFI file (SHA256
  `04bc11296d4f07b35f257adfc5ed9dbdc15fc9919e75302efca148a68d778f09`).
- `--enable-segue` is documented as linux x86-64 only. On RISC-V it is a
  silent no-op: the with-Segue and without-Segue files were byte-identical.
- `--size-level=0` selects the large code model rather than an O4-like
  optimization tier. That file could not load because WAMR's RISC-V AOT loader
  could not resolve the `.data.rel.ro` relocation.
- The `spacemit-x60` LLVM CPU preset loaded but hit an integer divide-by-zero
  exception in CoreMark. Using explicit `rv64imafdc` features produced the
  correct CRCs and is the validated configuration above.
- On this 64-bit runtime, software memory bounds checks are disabled by
  default in favor of hardware traps. Explicit no-SFI flags changed throughput
  from 7,250.711476 to 7,254.788160 iterations/s (0.06%, run-to-run noise).

The fast-interpreter 400,000-iteration run was stopped after more than four
minutes to prioritize the requested AOT variants; no interpreter score is
reported.
