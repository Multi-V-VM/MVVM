use alloc::{boxed::Box, collections::BTreeMap, format, string::String, vec::Vec};
use goblin::elf::{header, program_header, Elf, Reloc};
use std::{
    cmp,
    convert::{TryFrom, TryInto},
    dbg, fmt,
    ops::{Deref, Range},
    println,
};
#[derive(Debug)]
pub enum ELFError {
    GoblinError(goblin::error::Error),
    AddressError(String),
}

impl From<goblin::error::Error> for ELFError {
    fn from(e: goblin::error::Error) -> Self {
        Self::GoblinError(e)
    }
}
impl From<String> for ELFError {
    fn from(e: String) -> Self {
        Self::AddressError(e)
    }
}

type ParseResult<T> = Result<T, ELFError>;

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

// pub struct InstructionIter<'a>{

// }

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
    fn valid_page_index_and_offset(addr: u64) -> Result<PageIndexOfs<usize, usize>, ELFError> {
        let res: PageIndexOfs<u64, usize> = Self::page_index_and_offset(addr);
        if res.page_index < Self::VALID_SIZE as u64 {
            Ok(res.map_page_index(|v| v as usize))
        } else {
            Err(ELFError::AddressError(
                format!("Invalid address: {}", addr,),
            ))
        }
    }
}

pub struct Binary<'a> {
    entry: usize,
    pages: Vec<Option<Page<'a>>>,
}
