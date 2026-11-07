#!/usr/bin/env python3
"""Generate reference outputs by executing the source Wasm on Wasmtime/CPU."""
import pathlib,sys,random,struct,json,hashlib
import wasmtime
out=pathlib.Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
rng=random.Random(9821);metadata={}
for ty in ['f32x4','i32x4']:
 for op in ['add','sub','mul']:
  name=ty+'_'+op
  wat=f'''(module (memory (export "memory") 1)
   (func (export "op") (param i32 i32 i32)
    local.get 2 local.get 0 v128.load local.get 1 v128.load {ty}.{op} v128.store))'''
  wasm=wasmtime.wat2wasm(wat);(out/(name+'.wasm')).write_bytes(wasm)
  engine=wasmtime.Engine();store=wasmtime.Store(engine);inst=wasmtime.Instance(store,wasmtime.Module(engine,wasm),[])
  mem=inst.exports(store)['memory'];fn=inst.exports(store)['op'];rows=[]
  for i in range(64):
   if ty=='f32x4':data=struct.pack('<8f',*(rng.uniform(-50,50) for _ in range(8)))
   else:data=struct.pack('<8I',*(rng.getrandbits(32) for _ in range(8)))
   mem.write(store,data,0);fn(store,0,16,32);expected=bytes(mem.read(store,32,48));rows.append(data+expected)
  raw=b''.join(rows);(out/(name+'.cases')).write_bytes(raw)
  metadata[name]={'cases':64,'wasm_sha256':hashlib.sha256(wasm).hexdigest(),'cases_sha256':hashlib.sha256(raw).hexdigest()}
(out/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')
