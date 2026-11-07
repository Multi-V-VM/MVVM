#!/usr/bin/env python3
"""Native, separate-process DWARF continuation tests and repeated timings."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess

p=argparse.ArgumentParser()
p.add_argument("runner",type=Path)
p.add_argument("artifacts",type=Path)
p.add_argument("output",type=Path)
p.add_argument("--cross-from",type=Path)
p.add_argument("--repeats",type=int,default=5)
p.add_argument("--cpu",type=int,help="Pin the experiment and child processes to one native CPU")
a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
arch=platform.machine()
if a.cpu is not None:
    os.sched_setaffinity(0,{a.cpu})
rows=[]
def invoke(name,mode,n,point,snapshot,variant="dwarf",expect_failure=False,dso_override=None):
    stem=a.artifacts/f"{name}.{arch}"
    aot=Path(str(stem)+(".o4-segue.aot" if variant=="o4-segue" else ".aot"))
    dso=str(stem)+".so" if variant=="dwarf" else "-"
    if dso_override: dso=str(dso_override)
    command=[str(a.runner),str(a.artifacts/f"{name}.wasm"),str(aot),dso,mode,str(snapshot),str(n),str(point)]
    result=subprocess.run(command,text=True,capture_output=True,timeout=180)
    if expect_failure:
        assert result.returncode!=0,command
        row={"workload":name,"negative":True,"stderr":result.stderr,"returncode":result.returncode}
    else:
        assert result.returncode==0,(command,result.stdout,result.stderr)
        row=json.loads(next(line for line in result.stdout.splitlines() if line.startswith('{"mode"')))
        row.update(workload=name,variant=variant,point=point,arch=arch)
        assert row["result"]!=0xffffffff,("CoreMark CRC failed",row)
    rows.append(row)
    with (a.output/"raw.jsonl").open("a") as f: f.write(json.dumps(row)+"\n")
    return row

def equal(actual,expected):
    assert (actual["result"],actual["effects"])==(expected["result"],expected["effects"]),(actual,expected)

if a.cross_from:
    source=json.loads((a.cross_from/"summary.json").read_text())
    for case in source["correctness"]:
        restored=invoke(case["workload"],"restore",case["iterations"],case["point"],a.cross_from/case["snapshot"])
        equal(restored,case["expected"])
    summary={"host":platform.node(),"arch":arch,"source_arch":source["arch"],"cross_isa_cases_passed":len(rows),"rows":rows}
else:
    correctness=[]
    for name,points in (("indirect",[1,2,49,50,99,100]),("coremark",[1,50,513,1024,51200,102400])):
        expected=invoke(name,"baseline",100,1,a.output/"unused",variant="plain")
        equal(invoke(name,"baseline",100,1,a.output/"unused"),expected)
        for point in points:
            filename=f"{name}-{point}.snapshot"
            saved=invoke(name,"save",100,point,a.output/filename)
            assert saved["effects"]==point and saved["frames"]>=3 and saved["slots"]>0,saved
            restored=invoke(name,"restore",100,point,a.output/filename)
            equal(restored,expected)
            correctness.append({"workload":name,"iterations":100,"point":point,"snapshot":filename,"sha256":hashlib.sha256((a.output/filename).read_bytes()).hexdigest(),"expected":expected})
    # Deserialization, source identity, and missing native DWARF must fail.
    truncated=a.output/"truncated.snapshot"
    truncated.write_bytes((a.output/"indirect-50.snapshot").read_bytes()[:-7])
    invoke("indirect","restore",100,50,truncated,expect_failure=True)
    invoke("indirect","restore",100,50,a.output/"coremark-50.snapshot",expect_failure=True)
    invoke("indirect","restore",101,50,a.output/"indirect-50.snapshot",expect_failure=True)
    for kind in ("pc","slot","schema"):
        mutated=a.output/f"invalid-{kind}.snapshot"
        subprocess.run([str(a.runner.parent/"mvvm_dwarf_snapshot_mutator"),str(a.output/"indirect-50.snapshot"),str(mutated),kind],check=True)
        invoke("indirect","restore",100,50,mutated,expect_failure=True)
    stripped=a.output/f"stripped.{arch}.so"
    subprocess.run(["objcopy","--strip-debug",str(a.artifacts/f"indirect.{arch}.so"),str(stripped)],check=True)
    invoke("indirect","save",100,50,a.output/"should-not-exist",expect_failure=True,dso_override=stripped)
    timings=[]
    for name,n,point in (("indirect",1000000,500000),("coremark",1000,512000)):
        baseline=None
        for variant in ("plain","o4-segue","dwarf"):
            series=[]
            for _ in range(a.repeats):
                row=invoke(name,"baseline",n,point,a.output/"unused",variant=variant)
                if baseline is None: baseline=row
                equal(row,baseline)
                series.append(row["run_ms"])
            timings.append({"workload":name,"variant":variant,"iterations":n,"samples_ms":series,"median_ms":statistics.median(series),"min_ms":min(series),"max_ms":max(series)})
        for rep in range(a.repeats):
            snapshot=a.output/f"timing-{name}-{rep}.snapshot"
            invoke(name,"save",n,point,snapshot)
            equal(invoke(name,"restore",n,point,snapshot),baseline)
    summary={"host":platform.node(),"arch":arch,"cpu":a.cpu,"correctness":correctness,"correctness_cases_passed":len(correctness),"negative_cases_passed":7,"timings":timings,"repeats":a.repeats}
(a.output/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
print(json.dumps(summary,indent=2),flush=True)
