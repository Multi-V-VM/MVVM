use crate::frontend::elf::{Class, Machine, ProgramHeaderType};
use crate::frontend::BIT_LENGTH;

use super::elf::{Data, ElfError, ElfFile, ParseResult, Type};
use super::page::Page;
use std::ops::Range;

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

        unsafe {
            match elf.header_part1.get_class() {
                Class::ThirtyTwo => BIT_LENGTH = 0,
                Class::SixtyFour => BIT_LENGTH = 1,
                Class::OneTwentyEight => BIT_LENGTH = 2,
                _ => return Err(ElfError::NotMeet(String::from("Not expected Class Binary"))),
            }
        }
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
            String::from("Not RISCV Dynamatic Linked")
        );
        let program_iter = elf.program_iter();
        // iterate the program and iterate the bytes
        for ph in program_iter {
            dbg!(" {}", ph);
            if ph.get_type() != ProgramHeaderType::Load {
                continue;
            }
            let address_range = ph.get_virtual_addr()
                ..ph.get_virtual_addr()
                    .checked_add(ph.get_mem_size())
                    .unwrap_or_else(|| {
                        panic!("invalid PT_LOAD program header virtual address range");
                    });
            let bytes_start = ph.get_offset() as usize;
            let bytes_end = bytes_start
                .checked_add(ph.get_file_size() as usize)
                .unwrap_or_else(|| panic!("invalid PT_LOAD program header file byte range"));
            let bytes = &bytes[bytes_start..bytes_end];
            // set the range of addresses in `address_range` to bytes in `bytes`
            if address_range.end > Page::ADDRESS_LIMIT {
                panic!("tried to map address range that is larger than the sv39 limit 0x{:X}..0x{:X} limit=0x{:X}",address_range.start,address_range.end,Page::ADDRESS_LIMIT);
            }
            if !address_range.is_empty() {
                let mut page_range =
                    Page::valid_page_range_index_and_offset(address_range.clone())?;
                let address_range_len = address_range.end as usize - address_range.start as usize;
                fn map_page<'a>(
                    page: &mut Option<Page<'a>>,
                    offset_range: Range<usize>,
                    bytes: &'a [u8],
                ) -> ParseResult<()> {
                    let bytes = &bytes[..bytes.len().min(offset_range.len())];
                    if offset_range == (0..Page::SIZE) {
                        match bytes.len() {
                            Page::SIZE => {
                                *page = Some(Page::Borrowed(bytes.try_into().unwrap()));
                                return Ok(());
                            }
                            0 => {
                                *page = Some(Page::Borrowed(&[0; Page::SIZE]));
                                return Ok(());
                            }
                            _ => {}
                        }
                    }
                    match page {
                        None => {
                            let mut page_bytes = Vec::with_capacity(Page::SIZE);
                            page_bytes.resize(offset_range.start, 0);
                            page_bytes.extend(bytes);
                            page_bytes.resize(Page::SIZE, 0);
                            *page = Some(Page::Owned(
                                page_bytes.into_boxed_slice().try_into().unwrap(),
                            ));
                        }
                        Some(data @ Page::Borrowed(_)) => {
                            let mut page_bytes = Vec::with_capacity(Page::SIZE);
                            let old_bytes = match *data {
                                Page::Borrowed(bytes) => bytes,
                                Page::Owned(_) => panic!("Should be borrowed for pages"),
                            };
                            page_bytes.extend(&old_bytes[..offset_range.start]);
                            page_bytes.extend(bytes);
                            page_bytes.resize(offset_range.end, 0);
                            page_bytes.extend(&old_bytes[offset_range.end..]);
                            *data = Page::Owned(page_bytes.into_boxed_slice().try_into().unwrap());
                        }
                        Some(Page::Owned(data)) => {
                            data[offset_range.start..][..bytes.len()].copy_from_slice(bytes);
                            data[offset_range][bytes.len()..].fill(0);
                        }
                    }
                    Ok(())
                }
            }
        }
        let mut section_iter = elf.section_iter();
        dbg!(
            "Section header size: {}",
            elf.header_part2.get_sh_count() - 1
        );
        section_iter.next();
        for sh in section_iter {
            dbg!(" {}", sh.get_data(&elf));
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
        unsafe { assert_eq!(BIT_LENGTH, 1) }
    }
}
