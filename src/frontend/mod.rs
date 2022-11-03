#[allow(unreachable_code)]
pub mod binary;
pub mod context;
pub mod elf;
pub mod instruction;
pub mod page;

use lazy_static::lazy_static;

lazy_static! {
    static ref BIT_LENGTH: i8 = 0;
    static ref IS_E: bool = false;
}

// static mut BIT_LENGTH: i8 = 0;
// static mut IS_E: bool = false;
