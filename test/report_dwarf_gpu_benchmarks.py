#!/usr/bin/env python3
"""Render measured paired-mode results; refuses different workloads/binaries."""
import argparse,json,pathlib
p=argparse.ArgumentParser();p.add_argument('cc',type=pathlib.Path);p.add_argument('plain',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args()
cc=json.loads(a.cc.read_text());plain=json.loads(a.plain.read_text())
assert cc['mode']=='cc' and plain['mode']=='plain'
for k in ('workload_sha256','runner_sha256','repeats'):
    assert cc[k]==plain[k],f'mismatched {k}'
def median(row,key):return row['statistics'][key]['median']
by_n={r['iterations']:r for r in plain['benchmarks']}
lines=['Measured medians; times in milliseconds. Both modes use the same Wasm and runner binary.',
       '', '| Iterations | KVM / CC-Off AOT | TDX / CC-On AOT | TDX / CC-On DWARF | Combined CC overhead (AOT) | DWARF overhead (CC-On) |',
       '|---:|---:|---:|---:|---:|---:|']
for r in cc['benchmarks']:
    n=r['iterations'];off=median(by_n[n],'aot_ms');on=median(r,'aot_ms');dw=median(r,'dwarf_ms')
    lines.append(f'| {n} | {off:.3f} | {on:.3f} | {dw:.3f} | {(on/off-1)*100:+.2f}% | {(dw/on-1)*100:+.2f}% |')
lines+=['','Combined CC overhead includes TDX, GPU CC and the strict runtime path; it does not isolate GPU encryption.','',
        '| Mode | Iterations | Uninterrupted process | Save + restore processes | Added migration time (paired median) | Migration overhead (ratio of medians) | Destination recovery | Snapshot bytes |',
        '|---|---:|---:|---:|---:|---:|---:|---:|']
for summary in (plain,cc):
    for r in summary['benchmarks']:
        base=median(r,'baseline_wall_ms');mig=median(r,'migration_wall_ms')
        lines.append(f"| {summary['mode']} | {r['iterations']} | {base:.3f} | {mig:.3f} | {median(r,'migration_extra_ms'):.3f} | {(mig/base-1)*100:+.2f}% | {median(r,'recovery_wall_ms'):.3f} | {int(median(r,'snapshot_bytes'))} |")
lines+=['','| Mode | Iterations | CPU capture | GPU capture + outer encryption | GPU restore | Native stack restore | Save-process attestation | Restore-process attestation |',
        '|---|---:|---:|---:|---:|---:|---:|---:|']
for summary in (plain,cc):
    for r in summary['benchmarks']:
        values=[median(r,k) for k in ('cpu_capture_ms','gpu_capture_ms','gpu_restore_ms','stack_restore_ms','save_attest_ms','restore_attest_ms')]
        lines.append(f"| {summary['mode']} | {r['iterations']} | "+' | '.join(f'{v:.3f}' for v in values)+' |')
lines+=['','Attestation is included in the relevant phase and process totals, not added again.',
        'The workload launches four SIMD lanes at a time and keeps 1 MiB of additional device state. These are small-kernel/process-migration measurements, not GPU peak-throughput or LLM results.',
        f"Each mode passed first/middle/last checkpoint recovery and {cc['negative_checks']} negative cases; each timing uses {cc['repeats']} repetitions. Raw samples and min/max are retained in the JSON artifacts."]
a.output.write_text('\n'.join(lines)+'\n')
