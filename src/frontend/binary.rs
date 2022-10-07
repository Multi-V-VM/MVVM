use crate::frontend::elf::{Class, Machine};

use super::elf::{Data, ElfError, ElfFile, ParseResult, Type};
use super::page::Page;

/// From ELF Byte to Page
pub struct Binary<'a> {
    pages: Vec<Option<Page<'a>>>,
}

macro_rules! parse_not_meet{
    ($first:expr, $second:expr , $($msg:tt)*)
    => {
        if $first != $second {
            return Err(ElfError::NotMeet($($msg)*))
        }
    };
}

impl<'a> Binary<'a> {
    pub fn parse(bytes: &'a [u8]) -> ParseResult<Self> {
        let mut elf = ElfFile::new(bytes).unwrap();
        let pages = Vec::new();
        parse_not_meet!(
            elf.header_part1.get_class(),
            Class::SixtyFour,
            String::from("Not 64bit Binary")
        );
        parse_not_meet!(
            elf.header_part1.get_data(),
            Data::LittleEndian,
            String::from("Not little endian Binary")
        );
        parse_not_meet!(
            elf.header_part2.get_type(),
            Type::Executable,
            String::from("Not Executable")
        );
        parse_not_meet!(
            elf.header_part2.get_machine(),
            Machine::RISCV,
            String::from("Not RISCV Binary")
        );
        parse_not_meet!(
            elf.parse_interpreter().unwrap(),
            "/lib/ld-linux-riscv64-lp64d.so.1",
            String::from("Not RISCV Dynamic Linked")
        );
        dbg!("{}", elf.header_part1);
        dbg!("{}", elf.header_part2);
        let program_iter = elf.program_iter();
        dbg!("Program header size: {}", elf.header_part2.get_ph_count());
        for ph in program_iter {
            dbg!(" {}", ph);
        }
        let mut section_iter = elf.section_iter();
        dbg!(
            "Section header size: {}",
            elf.header_part2.get_sh_count() - 1
        );
        section_iter.next();
        for sh in section_iter {
            dbg!(" {}", sh);
        }

        Ok(Self { pages })
    }
}

#[cfg(test)]
mod test {
    use super::*;
    #[test]
    fn test_parse_naive_binary() {
        Binary::parse(include_bytes!(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/test_binaries/test1"
        )))
        .unwrap();
    }
}
