# CXL checkpoint streams

MVVM's CXL stream writes the `struct_pack` checkpoint directly into one shared
mapping. During restore, `read_view()` makes WAMR's linear memory a span over
those same bytes. The app heap is represented as an offset and length inside
linear memory, so it is not serialized or allocated a second time.

Two hardware layouts are supported:

- CXL memory already onlined by firmware as a System-RAM NUMA node. MVVM uses
  `memfd_create`, `MAP_SHARED`, and `mbind` and never creates a DAX device.
- A filesystem-DAX file. This remains available for machines that deliberately
  configured a DAX region.

Use the NUMA path when `numactl --hardware` already shows the CXL capacity as a
memory-only node. Binding only the checkpoint mapping is more precise than
starting the whole VM with `numactl --membind`: ordinary VM allocations stay
under their existing policy, while the shared checkpoint is forced onto CXL.

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
`MAP_SHARED` behavior on machines without CXL. This mode never reports itself
as hardware CXL.

For CXL exposed as System RAM, first identify the memory-only node and then run
a large checkpoint. This does not create, reconfigure, format, or mount DAX:

```sh
numactl --hardware
./build-cxl/mvvm_cxl_fork_stream_test \
  --numa-node 1 \
  --memory-bytes 536870912
```

The NUMA backend pre-populates its reusable shared mapping on the selected node
before checkpoint timing begins. The child verifies the physical page location
with `move_pages`, restores the linear-memory span without copying it, and
proves sharing by mutating a byte that the parent observes.

To run the same test on real filesystem-DAX storage:

```sh
./build-cxl/mvvm_cxl_fork_stream_test /mnt/daxfs/mvvm-test.cxl
```

The filesystem-DAX path is strict: MVVM requires `STATX_ATTR_DAX` and maps with
`MAP_SHARED_VALIDATE | MAP_SYNC`. A normal ext4/tmpfs file is rejected rather
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
