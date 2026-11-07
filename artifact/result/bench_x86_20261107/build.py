from pathlib import Path
import subprocess,json,sys,time
b=Path('/root/mvvm-bench-suite'); out=b/'artifacts';out.mkdir(exist_ok=True)
wamrc='/root/mvvm-gpu-cc-20261107/wamrc-build/wamrc'
nas=b/'prepared/nas'
status={}
for name in sys.argv[1:] or ['ep','cg','ft','mg','bt','lu','sp','is','linpack']:
    t=time.time();log=(b/(name+'-build.log')).open('w')
    def call(args):
        log.write('COMMAND '+json.dumps(list(map(str,args)))+'\n');log.flush()
        subprocess.run(list(map(str,args)),stdout=log,stderr=log,check=True,timeout=600)
    try:
        if name=='linpack': sources=[b/'prepared/linpack/linpack.c'];inc=[]
        else:
            sources=[nas/name.upper()/(name+'.c'), b/'wrapper.c', nas/'common/c_print_results.c',nas/'common/c_timers.c',nas/'common/wtime.c']
            if name!='is':sources+=[nas/'common/c_randdp.c']
            inc=['-I'+str(nas/'common'),'-I'+str(nas/name.upper())]
        call(['gcc-16','-O3','-std=gnu99','-march=x86-64','-ffp-contract=off','-Wno-attributes',*inc,b/'native.c',*sources,'-lm','-o',out/(name+'.gcc')])
        call(['/opt/wasi-sdk/bin/clang-21','--target=wasm32-wasi','--sysroot=/root/mvvm-bench-suite/wasi-libc/usr','-O3','-std=gnu99','-ffp-contract=off','-mno-reference-types','-mexec-model=reactor',*inc,*sources,'-Wl,--export=run','-Wl,--export=effects','-Wl,-z,stack-size=8388608','-lm','-o',out/(name+'.wasm')])
        common=[wamrc,'--target=x86_64','--cpu=x86-64','--size-level=3','--disable-simd','--bounds-checks=0','--stack-bounds-checks=0','--disable-aux-stack-check']
        for variant,extra in [('aot',['--opt-level=3']),('segue.aot',['--opt-level=4','--enable-segue']),('ll',['--opt-level=3','--format=llvmir-opt','--mllvm=-mvvm-dwarf-resume','--mllvm=-mvvm-dwarf-optimize'])]:
            call([*common,*extra,'-o',out/(name+'.'+variant),out/(name+'.wasm')])
        call(['llc-20','-O3','-filetype=obj','-relocation-model=pic','-generate-arange-section',out/(name+'.ll'),'-o',out/(name+'.o')])
        call(['gcc-16','-shared',out/(name+'.o'),'-o',out/(name+'.so')])
        status[name]={'status':'built','seconds':time.time()-t}
    except Exception as e:status[name]={'status':'failed','error':str(e)}
    log.close();print(name,status[name],flush=True)
    (b/'build-status.json').write_text(json.dumps(status,indent=2)+'\n')
