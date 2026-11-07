// SPDX-License-Identifier: Apache-2.0
//! Validated, bounded SIMD ABI for the MVVM CUDA backend. One Wasm vector
//! operation maps to four CUDA lanes; packed inputs contain A[4] then B[4].
use wasmparser::{ExternalKind, Operator, Parser, Payload, ValType};

pub fn lower(
    wasm: &[u8],
    requested: Option<&str>,
    sm: u32,
    tenant: u64,
) -> Option<(String, String)> {
    let mut types_ok = false;
    let mut functions_ok = false;
    let mut memory_ok = false;
    let mut export = None;
    let mut instruction = None;
    for item in Parser::new(0).parse_all(wasm) {
        match item.ok()? {
            Payload::TypeSection(reader) => {
                let types: Vec<_> = reader
                    .into_iter_err_on_gc_types()
                    .collect::<Result<_, _>>()
                    .ok()?;
                types_ok = types.len() == 1
                    && types[0].params() == [ValType::I32; 3]
                    && types[0].results().is_empty();
            }
            Payload::FunctionSection(reader) => {
                let functions: Vec<_> = reader.into_iter().collect::<Result<_, _>>().ok()?;
                functions_ok = functions == [0];
            }
            Payload::MemorySection(reader) => {
                let memories: Vec<_> = reader.into_iter().collect::<Result<_, _>>().ok()?;
                memory_ok = memories.len() == 1
                    && !memories[0].shared
                    && !memories[0].memory64
                    && memories[0].initial >= 1;
            }
            Payload::ImportSection(_) | Payload::StartSection { .. } => return None,
            Payload::ExportSection(reader) => {
                for e in reader {
                    let e = e.ok()?;
                    if e.kind == ExternalKind::Func
                        && e.index == 0
                        && requested.map_or(true, |n| n == e.name)
                    {
                        export = Some(e.name.to_owned());
                    }
                }
            }
            Payload::CodeSectionEntry(body) => {
                if instruction.is_some() || body.get_locals_reader().ok()?.get_count() != 0 {
                    return None;
                }
                let mut r = body.get_operators_reader().ok()?;
                if !matches!(r.read().ok()?, Operator::LocalGet { local_index: 2 })
                    || !matches!(r.read().ok()?, Operator::LocalGet { local_index: 0 })
                {
                    return None;
                }
                let load = r.read().ok()?;
                if !matches!(load,Operator::V128Load{memarg} if memarg.offset==0&&memarg.memory==0)
                {
                    return None;
                }
                if !matches!(r.read().ok()?, Operator::LocalGet { local_index: 1 }) {
                    return None;
                }
                if !matches!(r.read().ok()?,Operator::V128Load{memarg} if memarg.offset==0&&memarg.memory==0)
                {
                    return None;
                }
                instruction = Some(match r.read().ok()? {
                    Operator::F32x4Add => ("f32", "add.rn.f32"),
                    Operator::F32x4Sub => ("f32", "sub.rn.f32"),
                    Operator::F32x4Mul => ("f32", "mul.rn.f32"),
                    Operator::I32x4Add => ("u32", "add.u32"),
                    Operator::I32x4Sub => ("u32", "sub.u32"),
                    Operator::I32x4Mul => ("u32", "mul.lo.u32"),
                    _ => return None,
                });
                if !matches!(r.read().ok()?,Operator::V128Store{memarg} if memarg.offset==0&&memarg.memory==0)
                    || !matches!(r.read().ok()?, Operator::End)
                    || !r.eof()
                {
                    return None;
                }
            }
            _ => {}
        }
    }
    if !(types_ok && functions_ok && memory_ok) {
        return None;
    }
    let name = export?;
    let (ty, op) = instruction?;
    let ptx = format!(
        r#"// mvvm-checked-simd-v1; tenant={tenant}
.version 8.7
.target sm_{sm}
.address_size 64
.visible .entry mvvm_simd(
    .param .u64 input,
    .param .u64 output,
    .param .u32 count
)
{{
    .reg .pred p;
    .reg .u32 lane, block;
    .reg .u64 src, dst, offset;
    .reg .{ty} a, b, c;
    mov.u32 block, %ctaid.x;
    setp.ne.u32 p, block, 0;
    @p bra done;
    mov.u32 lane, %tid.x;
    setp.ge.u32 p, lane, 4;
    @p bra done;
    ld.param.u64 src, [input];
    ld.param.u64 dst, [output];
    mul.wide.u32 offset, lane, 4;
    add.u64 src, src, offset;
    add.u64 dst, dst, offset;
    ld.global.{ty} a, [src];
    ld.global.{ty} b, [src+16];
    {op} c, a, b;
    st.global.{ty} [dst], c;
done:
    ret;
}}
"#
    );
    Some((name, ptx))
}

#[cfg(test)]
mod tests {
    use super::lower;
    fn module(op: &str, offset: &str) -> Vec<u8> {
        wat::parse_str(format!(
            r#"(module (memory 1)
      (func (export "op") (param i32 i32 i32)
        local.get 2 local.get 0 v128.load {offset} local.get 1 v128.load {op} v128.store))"#
        ))
        .unwrap()
    }
    #[test]
    fn checked_numeric_ops_and_abi() {
        for op in [
            "f32x4.add",
            "f32x4.sub",
            "f32x4.mul",
            "i32x4.add",
            "i32x4.sub",
            "i32x4.mul",
        ] {
            let wasm = module(op, "");
            wasmparser::Validator::new().validate_all(&wasm).unwrap();
            let (name, ptx) = lower(&wasm, Some("op"), 120, 42).unwrap();
            assert_eq!(name, "op");
            assert!(ptx.contains(".target sm_120"));
            assert!(ptx.contains("[src+16]"));
            assert!(lower(&wasm, Some("missing"), 120, 42).is_none());
        }
    }
    #[test]
    fn rejects_nonzero_offsets_and_unmodelled_operations() {
        assert!(lower(&module("f32x4.add", "offset=4"), Some("op"), 120, 0).is_none());
        assert!(lower(&module("f32x4.div", ""), Some("op"), 120, 0).is_none());
    }
}
