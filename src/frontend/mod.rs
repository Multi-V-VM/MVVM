#[allow(unreachable_code)]
pub mod binary;
pub mod cache;
pub mod elf;
pub mod instruction;
pub mod page;

static mut BIT_LENGTH: i8 = 0;
static mut IS_E: bool = false;
