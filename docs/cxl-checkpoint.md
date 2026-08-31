# CXL/DAX checkpoint stream

MVVM's CXL stream writes the `struct_pack` checkpoint directly into one
`MAP_SHARED` DAX mapping.  During restore, `read_view()` makes WAMR's linear
memory a span over those same bytes.  The app heap is represented as an offset
and length inside linear memory, so it is not serialized or allocated a second
time.

## Build and functional fork test

```sh
cmake -S . -B build-cxl \
  -DMVVM_ENABLE_CXL=ON \
  -DMVVM_BUILD_TEST=ON \
  -DMVVM_ENABLE_TENSOR_WASM_JIT=OFF
cmake --build build-cxl --target mvvm_cxl_fork_stream_test -j
ctest --test-dir build-cxl -R mvvm_cxl_fork_stream --output-on-failure
```

Without an argument the test uses `memfd_create` only to validate the fork and
`MAP_SHARED` behavior on machines without CXL.  This mode never reports itself
as hardware CXL.  To run the same test on real filesystem-DAX storage:

```sh
./build-cxl/mvvm_cxl_fork_stream_test /mnt/daxfs/mvvm-test.cxl
```

The DAX path is strict: MVVM requires `STATX_ATTR_DAX` and maps with
`MAP_SHARED_VALIDATE | MAP_SYNC`.  A normal ext4/tmpfs file is rejected rather
than silently treated as CXL.

## Checkpoint and restore commands

Choose a capacity large enough for the whole serialized VM state:

```sh
MVVM_checkpoint --target app.wasm \
  --cxl-file /mnt/daxfs/app.cxl \
  --cxl-capacity 4294967296

MVVM_restore --target app.wasm \
  --cxl-file /mnt/daxfs/app.cxl
```

The `ReadStream` or owning WAMR process must retain the mapping for the full
lifetime of the restored execution environment because its linear-memory span
points into that mapping.
