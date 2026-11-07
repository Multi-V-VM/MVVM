Measured medians; times in milliseconds. Both modes use the same Wasm and runner binary.

| Iterations | KVM / CC-Off AOT | TDX / CC-On AOT | TDX / CC-On DWARF | Combined CC overhead (AOT) | DWARF overhead (CC-On) |
|---:|---:|---:|---:|---:|---:|
| 1000 | 27.495 | 69.628 | 69.035 | +153.24% | -0.85% |
| 10000 | 251.444 | 640.219 | 642.449 | +154.62% | +0.35% |

Combined CC overhead includes TDX, GPU CC and the strict runtime path; it does not isolate GPU encryption.

| Mode | Iterations | Uninterrupted process | Save + restore processes | Added migration time (paired median) | Migration overhead (ratio of medians) | Destination recovery | Snapshot bytes |
|---|---:|---:|---:|---:|---:|---:|---:|
| plain | 1000 | 296.822 | 617.436 | 322.121 | +108.02% | 301.413 | 2231800 |
| plain | 10000 | 529.476 | 839.834 | 317.035 | +58.62% | 413.538 | 2231800 |
| cc | 1000 | 4030.765 | 10115.735 | 6091.836 | +150.96% | 5034.625 | 2244155 |
| cc | 10000 | 4551.230 | 10740.713 | 6156.807 | +136.00% | 5369.535 | 2244155 |

| Mode | Iterations | CPU capture | GPU capture + outer encryption | GPU restore | Native stack restore | Save-process attestation | Restore-process attestation |
|---|---:|---:|---:|---:|---:|---:|---:|
| plain | 1000 | 16.848 | 9.277 | 0.858 | 1.590 | 0.000 | 0.000 |
| plain | 10000 | 16.033 | 8.688 | 0.862 | 1.518 | 0.000 | 0.000 |
| cc | 1000 | 20.484 | 1042.687 | 1028.857 | 1.774 | 2008.892 | 1995.440 |
| cc | 10000 | 19.974 | 1013.680 | 1028.622 | 1.912 | 2038.618 | 2018.341 |

Attestation is included in the relevant phase and process totals, not added again.
The workload launches four SIMD lanes at a time and keeps 1 MiB of additional device state. These are small-kernel/process-migration measurements, not GPU peak-throughput or LLM results.
Each mode passed first/middle/last checkpoint recovery and 3 negative cases; each timing uses 5 repetitions. Raw samples and min/max are retained in the JSON artifacts.
