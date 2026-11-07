#!/usr/bin/env python3
"""Matched before/after integrity-check timings with native save/restore checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("before_runner", type=Path)
p.add_argument("after_runner", type=Path)
p.add_argument("before_artifacts", type=Path)
p.add_argument("after_artifacts", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--cpu", type=int, default=8)
p.add_argument("--repeats", type=int, default=11)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
os.sched_setaffinity(0, {a.cpu})
arch = platform.machine()
rng = random.Random(20261107)
rows = []
identity = {}
for name in ("indirect", "coremark"):
    left = (a.before_artifacts / f"{name}.wasm").read_bytes()
    assert left == (a.after_artifacts / f"{name}.wasm").read_bytes()
    identity[name] = hashlib.sha256(left).hexdigest()

def invoke(version, name, n, point, mode, repetition):
    artifacts = a.before_artifacts if version == "before" else a.after_artifacts
    runner = a.before_runner if version == "before" else a.after_runner
    snapshot = a.output / f"{version}-{name}.snapshot"
    result = subprocess.run([str(runner), str(artifacts / f"{name}.wasm"),
        str(artifacts / f"{name}.{arch}.aot"), str(artifacts / f"{name}.{arch}.so"),
        mode, str(snapshot), str(n), str(point)], capture_output=True, text=True, timeout=180,
        env={k:v for k,v in os.environ.items() if k != "MVVM_GPU_MODE"})
    assert result.returncode == 0, (result.stdout, result.stderr)
    row = json.loads(next(s for s in result.stdout.splitlines() if s.startswith('{"mode"')))
    row.update(version=version, workload=name, repetition=repetition)
    if repetition >= 0:
        rows.append(row)
        with (a.output / "raw.jsonl").open("a") as f:
            f.write(json.dumps(row) + "\n")
    return row

for name, n, point in (("indirect",10000000,5000000),("coremark",10000,5120000)):
    expected = None
    for rep in range(-1, a.repeats):
        order = ["before", "after"]
        rng.shuffle(order)
        for version in order:
            baseline = invoke(version,name,n,point,"baseline",rep)
            current = (baseline["result"],baseline["effects"])
            if expected is None:
                expected = current
            assert expected == current and baseline["result"] != 0xffffffff
            saved = invoke(version,name,n,point,"save",rep)
            assert saved["effects"] == point and saved["frames"] >= 2
            restored = invoke(version,name,n,point,"restore",rep)
            assert (restored["result"],restored["effects"]) == expected

summary = {"host":platform.node(), "arch":arch, "cpu":a.cpu, "repeats":a.repeats,
           "seed":20261107, "warmups":1, "wasm_sha256":identity,
           "scope":"Incremental cost of the completeness check; CPU pinned, shared host, frequency unlocked.",
           "timings":[]}
for name in ("indirect","coremark"):
    for mode,metric in (("baseline","run_ms"),("save","capture_ms"),("restore","stack_restore_ms")):
        item = {"workload":name, "mode":mode, "metric":metric}
        for version in ("before","after"):
            series = [r[metric] for r in rows if r["workload"] == name and r["mode"] == mode and r["version"] == version]
            item[version] = {"samples_ms":series,"median_ms":statistics.median(series),
                             "min_ms":min(series),"max_ms":max(series)}
        item["change_percent"] = 100*(item["after"]["median_ms"]/item["before"]["median_ms"]-1)
        summary["timings"].append(item)
(a.output/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
print(json.dumps(summary,indent=2))
