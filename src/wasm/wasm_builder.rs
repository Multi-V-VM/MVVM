use wasmtime::*;
use wasmtime_jit::CompiledModule;
use wasmtime_wasi::{sync::WasiCtxBuilder, WasiCtx};

struct MyState {
    message: String,
    wasi: WasiCtx,
}

pub struct WasmExecutor {
    pub module: CompiledModule,
}

impl WasmExecutor {
    pub fn new(module: CompiledModule) -> WasmExecutor {
        let engine = Engine::default();
        let mut linker = Linker::new(&engine);
        wasmtime_wasi::add_to_linker(&mut linker, |state: &mut MyState| &mut state.wasi).unwrap();
        WasmExecutor { module: module }
    }
}
