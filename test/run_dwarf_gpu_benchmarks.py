#!/usr/bin/env python3
"""Separate-process real-Wasm/GPU continuation test and benchmark.
Environment: MVVM_GPU_MODE=cc|plain, MVVM_GPU_KEY_FILE and CC verifier settings.
Hardware mode changes are deliberately external to this script.
"""
import argparse, hashlib, json, os, pathlib, statistics, subprocess, time
p=argparse.ArgumentParser()
p.add_argument('--runner',required=True);p.add_argument('--workload',required=True)
p.add_argument('--output',required=True);p.add_argument('--repeats',type=int,default=5)
p.add_argument('--iterations',type=int,nargs='+',default=[1000,10000])
a=p.parse_args();out=pathlib.Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
w=pathlib.Path(a.workload).resolve();runner=str(pathlib.Path(a.runner).resolve())
records=[]
def run(mode,n,point=1,debug=True,snapshot=None,env=None,fail=None):
    snapshot=snapshot or out/'state.bin'
    cmd=[runner,str(w/'gpu.wasm'),str(w/'gpu.aot'),str(w/'gpu.so') if debug else '-',mode,str(snapshot),str(n),str(point)]
    t=time.perf_counter();r=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=240)
    wall=(time.perf_counter()-t)*1000
    index=len(records);(out/f'{index:03}.stderr').write_text(r.stderr)
    row={'command':cmd,'wall_ms':wall,'returncode':r.returncode,'stdout':r.stdout,'stderr_log':f'{index:03}.stderr'}
    if fail is not None:
        assert r.returncode!=0 and fail in r.stderr,(row,r.stderr)
        row['expected_failure']=fail
    else:
        assert r.returncode==0,(row,r.stderr)
        for line in r.stdout.splitlines():
            if line.startswith('{'):row.update(json.loads(line))
        assert row.get('gpu_mode')==os.environ['MVVM_GPU_MODE'],row
        if mode!='save':
            # Sum of (4*i+14)+(7*i+11)+(i+29), with i starting at zero.
            expected=(17+6*n*(n-1)+54*n)&0xffffffff
            assert row['result']==expected and row['effects']==n,row
        else:
            assert row['effects']==point and row['frames']>=2 and row['slots']>0,row
    if fail is None:
        assert row['gpu_launches']==(point if mode=='save' else n-point if mode=='restore' else n),row
    records.append(row);(out/'raw.json').write_text(json.dumps(records,indent=2)+'\n')
    return row
# Check first/middle/last suspension; output has not yet been downloaded to Wasm.
checks=[]
for point in (1,8,16):
    saved=run('save',16,point);restored=run('restore',16,point)
    assert saved['pid']!=restored['pid'];checks.append(point)
# Authentication and invocation failures must not reach the resumed program.
original=(out/'state.bin').read_bytes();bad=bytearray(original);bad[len(bad)//2]^=1
(out/'corrupt.bin').write_bytes(bad)
run('restore',16,16,snapshot=out/'corrupt.bin',fail='authentication failed')
wrong=out/'wrong.key';wrong.write_bytes(os.urandom(32));wrong.chmod(0o600)
env=dict(os.environ,MVVM_GPU_KEY_FILE=str(wrong))
run('restore',16,16,env=env,fail='authentication failed');wrong.unlink()
run('restore',17,16,fail='root invocation argument mismatch')
# Warm up before timed samples. Initial context/JIT/NRAS still included in wall time.
run('baseline',100)
summaries=[]
for n in a.iterations:
    rows=[]
    for rep in range(a.repeats):
        plain=run('baseline',n,debug=False)
        baseline=run('baseline',n)
        save=run('save',n,n//2);restore=run('restore',n,n//2)
        assert save['pid']!=restore['pid']
        rows.append({'aot_ms':plain['run_ms'],'dwarf_ms':baseline['run_ms'],
                     'baseline_wall_ms':baseline['wall_ms'],
                     'migration_wall_ms':save['wall_ms']+restore['wall_ms'],
                     'migration_extra_ms':save['wall_ms']+restore['wall_ms']-baseline['wall_ms'],
                     'recovery_wall_ms':restore['wall_ms'],
                     'gpu_init_ms':baseline['gpu_init_ms'],
                     'cpu_capture_ms':save['capture_ms'],'cpu_encode_ms':save['encode_ms'],
                     'gpu_capture_ms':save['gpu_capture_ms'],'gpu_restore_ms':restore['gpu_restore_ms'],
                     'stack_restore_ms':restore['stack_restore_ms'],
                     'save_attest_ms':save['attest_ms'],'restore_attest_ms':restore['attest_ms'],
                     'snapshot_bytes':save['bytes']})
    stats={k:{'median':statistics.median(r[k] for r in rows),'min':min(r[k] for r in rows),'max':max(r[k] for r in rows)} for k in rows[0]}
    summaries.append({'iterations':n,'samples':rows,'statistics':stats})
result={'mode':os.environ['MVVM_GPU_MODE'],'repeats':a.repeats,'checkpoint_checks':checks,
        'negative_checks':3,'workload_sha256':hashlib.sha256((w/'gpu.wasm').read_bytes()).hexdigest(),
        'runner_sha256':hashlib.sha256(pathlib.Path(runner).read_bytes()).hexdigest(),'benchmarks':summaries}
(out/'summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
