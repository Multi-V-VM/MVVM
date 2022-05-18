#![allow(clippy::needless_doctest_main)]
#![doc(html_logo_url = "https://avatars.githubusercontent.com/u/102379947?s=96&v=4")]
#![cfg_attr(documenting, feature(doc_cfg))]


mod elf;
mod parser;
mod wasm;
mod runtime;
mod codegen;
mod cache;