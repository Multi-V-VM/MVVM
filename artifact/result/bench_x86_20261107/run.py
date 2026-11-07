from pathlib import Path
import os,subprocess,json,time,statistics,random,traceback,hashlib,sys
b=Path('/root/mvvm-bench-suite'); art=b/'artifacts';out=Path(os.environ.get('SUITE_RESULT_DIR',str(b/'results')));out.mkdir(exist_ok=True)
runner=Path('/root/mvvm-gpu-cc-20261107/build/dwarf/mvvm_dwarf_runner')
os.sched_setaffinity(0,{8});env=dict(os.environ);env.pop('MVVM_GPU_MODE',None)
variants=['gcc','aot','segue','dwarf'];rng=random.Random(20261108)
rows=[json.loads(l) for l in (out/'raw.jsonl').read_text().splitlines()] if (out/'raw.jsonl').exists() else []
summary=json.loads((out/'summary.json').read_text())['benchmarks'] if (out/'summary.json').exists() else []
for name in sys.argv[1:] or ['ep','cg','ft','mg','bt','lu','sp','is','linpack']:
    try:
        n=32 if name=='linpack' else 1
        for phase,count in [('warmup',1),('measurement',7)]:
            for rep in range(count):
                order=variants.copy();rng.shuffle(order)
                for variant in order:
                    cmd=([str(art/(name+'.gcc')),str(n)] if variant=='gcc' else
                         [str(runner),str(art/(name+'.wasm')),str(art/(name+('.segue.aot' if variant=='segue' else '.aot'))),str(art/(name+'.so')) if variant=='dwarf' else '-', 'baseline',str(out/'unused'),str(n),'1'])
                    t=time.perf_counter();p=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=180)
                    label=f'{name}-{phase}-{rep}-{variant}';(out/(label+'.log')).write_text(p.stdout+'\nSTDERR:\n'+p.stderr)
                    row={'workload':name,'variant':variant,'phase':phase,'repetition':rep,'command':cmd,'wall_ms':(time.perf_counter()-t)*1000,'returncode':p.returncode,'log':label+'.log'}
                    metrics=[json.loads(l) for l in p.stdout.splitlines() if l.startswith('{')]
                    if metrics:row.update(metrics[-1])
                    rows.append(row)
                    with (out/'raw.jsonl').open('a') as f:f.write(json.dumps(row)+'\n')
                    assert p.returncode==0 and row.get('result')==4660 and row.get('effects')==(64 if name=='linpack' else 1),row
                    if name!='linpack':assert 'Verification    =               SUCCESSFUL' in p.stdout,row
        stats={}
        for variant in variants:
            samples=[r['run_ms'] for r in rows if r['workload']==name and r['phase']=='measurement' and r['variant']==variant]
            stats[variant]={'samples_ms':samples,'median_ms':statistics.median(samples),'min_ms':min(samples),'max_ms':max(samples)}
        summary.append({'workload':name,'status':'passed','class':'fixed 200x200, 32 rolled+unrolled pairs' if name=='linpack' else 'W','variants':stats})
        print(name,{v:round(stats[v]['median_ms'],3) for v in variants},flush=True)
    except Exception as e:
        summary.append({'workload':name,'status':'failed','error':str(e)});print(name,'FAILED',str(e),flush=True)
    (out/'summary.json').write_text(json.dumps({'arch':'x86_64','host':'6787p','cpu':8,'repeats':7,'warmups':1,'seed':20261108,'benchmarks':summary},indent=2)+'\n')
