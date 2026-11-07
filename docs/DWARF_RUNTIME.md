# MVVM DWARF runtime

This branch replaces the default dependency on WAMR's private interpreter and
AOT frame layouts with a public-API continuation record:

- WAMR 2.4.5 provides the portable WebAssembly function index and bytecode
  offset through `wasm_copy_callstack`.
- libunwind consumes the native DWARF CFI to capture program-counter and stack
  state without copying WAMR private structs.
- elfutils resolves those native addresses through DWARF line information.
- GCC 16 C++26 reflection enumerates checkpoint fields for the binary codec.
  The implementation uses `std::format`; it does not use `{fmt}`,
  Yalantinglibs, or `struct_pack`.

Configure the default DWARF-only build explicitly with:

```sh
CC=gcc-16 CXX=g++-16 cmake -S . -B build-dwarf \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-dwarf --parallel
ctest --test-dir build-dwarf --output-on-failure
```

The serialized checkpoint has a magic/version header, a compiler-generated
schema fingerprint, the portable WAMR call-stack records, DWARF-symbolized
native frames, and a linear-memory image. Restore validates the module identity
before copying memory and exposing the top portable resume point.

## Validation boundary

DWARF describes native unwind and variable locations. It does not itself define
how to recreate an arbitrary live native call stack on another ISA. The current
runtime therefore validates and restores the portable memory image and emits a
WAMR `(function_index, function_offset)` resume point. A later WAMR trampoline
must consume that point at a compiler-inserted safe point before transparent
cross-ISA continuation can be claimed. The test in this branch intentionally
checks the implemented capture/codec/memory-restore boundary and does not label
that future trampoline as complete.
