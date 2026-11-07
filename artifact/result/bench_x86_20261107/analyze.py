from pathlib import Path
import json, statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
b=Path(__file__).resolve().parent
nas=json.loads((b/'nas-results/summary.json').read_text())
lin=json.loads((b/'linpack-results/summary.json').read_text())
rows=[r for r in nas['benchmarks'] if r['status']=='passed']+lin['benchmarks']
assert len(rows)==9 and all(r['status']=='passed' for r in rows)
for r in rows:
 for v in r['variants'].values():
  assert len(v['samples_ms'])==7
  assert abs(statistics.median(v['samples_ms'])-v['median_ms'])<1e-8
for folder in ['nas-results','linpack-results']:
 records=[json.loads(l) for l in (b/folder/'raw.jsonl').read_text().splitlines()]
 valid=[r for r in records if r['returncode']==0]
 assert all(r['result']==4660 and r['effects']==(64 if r['workload']=='linpack' else 1) for r in valid)
combined={'host':'6787p','arch':'x86_64','cpu':8,'warmups':1,'repeats':7,'benchmarks':rows,'plot_excludes':['mg'],'linpack_dwarf':'No resumable functions; not a measured DWARF instrumentation overhead'}
(b/'summary.json').write_text(json.dumps(combined,indent=2)+'\n')
table=['| Benchmark | GCC O3 ms | WAMR O3 / GCC | Segue / GCC | DWARF / GCC | DWARF vs WAMR O3 |','|---|---:|---:|---:|---:|---:|']
for r in rows:
 v=r['variants'];base=v['gcc']['median_ms'];a=v['aot']['median_ms'];sg=v['segue']['median_ms'];d=v['dwarf']['median_ms']
 tail='— | —' if r['workload']=='linpack' else f'{d/base:.3f}× | {(d/a-1)*100:+.2f}%'
 table.append(f"| {r['workload'].upper()} | {base:.3f} | {a/base:.3f}× | {sg/base:.3f}× | {tail} |")
report='''# bench x86 slowdown experiment — 2026-11-07

6787p, Xeon 6710E, logical CPU 8. One warmup plus seven measured repetitions
per variant, shuffled order. Timing covers the workload invocation, including
its ordinary I/O/verification, but excludes process startup and module loading.
All eight NAS Class W programs passed their built-in numerical verification;
LINPACK 200x200 passed a solution-error bound of 1e-9 for 32 pairs of rolled
and unrolled solves. The full experiment has 288 successful process invocations.

1× is native GCC 16 -O3; lower is faster. The figure excludes MG at the user's
request. The full table and raw measurements below retain it.

'''+ '\n'.join(table)+'''

## Interpretation and limits

- The requested `--opt-level=4 --enable-segue` is effectively O3 + Segue:
  this WAMR compiler clamps levels above 3. All WAMR modes have software memory,
  stack/aux-stack checks and SIMD disabled, matching the earlier runtime setup.
- Native GCC 16 and the Clang 21/WASI input both use -O3 and -ffp-contract=off;
  native target is generic x86-64. GCC uses host libc/libm and Wasm uses an
  isolated Ubuntu wasi-libc package. Ratios include compiler/libc/ABI effects,
  not only VM dispatch overhead. The host was shared and frequency was not fixed.
- NAS execution includes the existing call structure, without extra per-loop
  checkpoint imports. The optimized DWARF pass emits 16–40 resumable functions
  per NAS module around imports/indirect calls. These are steady-state compiler
  instrumentation measurements, not validated checkpoint/restore or arbitrary
  mid-kernel migration. Small negative overheads are measured differences, not
  an established causal speedup from DWARF.
- The fixed-work LINPACK kernel has no native imports or indirect calls and
  the optimized pass emits zero resumable functions. Its DWARF-mode timing is
  retained in raw JSON but omitted from the DWARF bars/table to avoid claiming
  zero-cost migration. AOT slowdown is still measured and shown.
- The initial 256x256 LINPACK input hit a zero pivot in native GCC; the failed
  warmup is preserved, excluded from performance statistics, and replaced with
  the valid 200x200 input. No numerical kernels were changed.
- Existing NAS parameter headers had nonstandard sizes/iteration counts despite
  Class S comments. The repository setparams utility regenerated standard Class W
  parameters in an isolated copy. `prepared-source.patch.gz` records the exact
  wrapper, declaration and fixed-work adaptations. Original bench sources were not changed.

## Evidence

- `summary.json`: combined medians, min/max and all seven samples.
- `nas-results/raw.jsonl`, `linpack-results/raw.jsonl`: commands, return values,
  invocation/wall time and full stdout/stderr log references. Extract
  `run-logs.tar.gz` here to restore the referenced per-run logs.
- `manifest.json`: original/prepared source and binary hashes, pinned submodules,
  compiler identity and count of DWARF-resumable functions.
- `prepare.py`, `build.py`, `run.py`, `native.c`, `wrapper.c`: isolated experiment
  scripts used on 6787p, with the original source copied under
  `/root/mvvm-bench-suite/source/{nas,linpack}`.
'''
(b/'RESULTS.md').write_text(report)
print('\n'.join(table))
view=[r for r in rows if r['workload']!='mg']
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':11,'axes.spines.top':False,'axes.spines.right':False,'svg.fonttype':'none'})
fig,ax=plt.subplots(figsize=(13,6.4));x=np.arange(len(view));w=.24
colors=['#3478be','#23866b','#d78a35']
for j,(variant,label,color) in enumerate(zip(['aot','segue','dwarf'],['WAMR O3','O4 request + Segue*','DWARF O3'],colors)):
 vals=[];lo=[];hi=[]
 for r in view:
  v=r['variants'][variant];base=r['variants']['gcc']['median_ms']
  value=v['median_ms']/base
  if r['workload']=='linpack' and variant=='dwarf':value=np.nan
  vals.append(value);lo.append(v['min_ms']/base);hi.append(v['max_ms']/base)
 pos=x+(j-1)*w
 ax.bar(pos,vals,width=w*.92,color=color,label=label,zorder=3)
 for px,val,mn,mx in zip(pos,vals,lo,hi):
  if np.isnan(val):
   ax.text(px,.14,'N/A¹',ha='center',fontsize=9,color='#8b5923')
   continue
  ax.errorbar(px,val,yerr=[[max(0,val-mn)],[max(0,mx-val)]],fmt='none',ecolor='#283342',capsize=3,lw=1)
  ax.text(px,mx+.055,f'{val:.2f}',ha='center',va='bottom',fontsize=9)
ax.axhline(1,color='#596579',linestyle='--',linewidth=1,label='Native GCC O3 = 1×')
ax.set_xticks(x,[r['workload'].upper() for r in view]);ax.set_ylabel('Execution time / native GCC -O3')
ax.set_ylim(0,2.65);ax.set_axisbelow(True);ax.grid(axis='y',color='#e4e8ee')
fig.suptitle('bench/ CPU slowdown — x86-64',fontsize=19,fontweight='bold',y=.975)
fig.text(.5,.91,'6787p / Xeon 6710E · CPU 8 · 7 measured runs per variant · numerical checks passed',ha='center',color='#485568')
ax.legend(loc='upper center',bbox_to_anchor=(.5,1.18),ncol=4,frameon=False,fontsize=10)
fig.text(.065,.145,'Bars: medians. Whiskers: observed min–max, not confidence intervals. Lower is faster; MG is excluded from this view.',fontsize=10)
fig.text(.065,.106,'* WAMR clamps O4 to O3. Software bounds checks and SIMD are off. Compiler/libc differences are included.',fontsize=10)
fig.text(.065,.067,'¹ LINPACK has no resumable functions in this setup; its DWARF-mode result is not presented as migration overhead.',fontsize=10)
fig.text(.065,.028,'NAS: Class W, existing call structure. LINPACK: fixed 200×200 solves. No checkpoint/restore operation is timed.',fontsize=10)
fig.subplots_adjust(left=.07,right=.985,bottom=.235,top=.78)
fig.savefig(b/'x86_bench_slowdown_no_mg.png',dpi=180,facecolor='white')
fig.savefig(b/'x86_bench_slowdown_no_mg.svg',facecolor='white')
