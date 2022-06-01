use super::elf::ElfError;


#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub struct PageIndexOfs<I, O> {
    pub page_index: I,
    pub offset: O,
}

impl<I, O> PageIndexOfs<I, O> {
    pub fn map_page_index<R, F: FnOnce(I) -> R>(self, f: F) -> PageIndexOfs<R, O> {
        let PageIndexOfs { page_index, offset } = self;
        PageIndexOfs {
            page_index: f(page_index),
            offset,
        }
    }
}

#[derive(Debug)]
pub enum Page<'a> {
    Borrowed(&'a [u8; Page::SIZE]),
    Owned(Box<[u8; Page::SIZE]>),
}
impl<'a> Page<'a> {
    const SIZE: usize = 1 << 12;
    const VALID_SIZE: usize = 1 << (38 - 12);
    const fn page_index_and_offset(addr: u64) -> PageIndexOfs<u64, usize> {
        PageIndexOfs {
            page_index: addr >> 12,
            offset: addr as usize % Self::SIZE,
        }
    }
    fn valid_page_index_and_offset(addr: u64) -> Result<PageIndexOfs<usize, usize>, ElfError> {
        let res: PageIndexOfs<u64, usize> = Self::page_index_and_offset(addr);
        if res.page_index < Self::VALID_SIZE as u64 {
            Ok(res.map_page_index(|v| v as usize))
        } else {
            Err(ElfError::AddressError(
                addr,"Invalid address".to_string(),
            ))
        }
    }
}
