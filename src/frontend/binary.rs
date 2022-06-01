use super::page::Page;
use super::elf::ParseResult;

/// From ELF Byte to Page
pub struct Binary<'a> {
    entry: usize,
    pages: Vec<Option<Page<'a>>>,
}

impl<'a> Binary<'a>{
    pub fn parse(bytes:&'a [u8])->ParseResult<Self>{
        
        todo!()
    }
}

#[cfg(test)]
mod test{
    use super::*;
    #[test]
    fn test_parse_naive_binary(){
        Binary::parse(include_bytes!(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/test_binaries/test1"
        )))
        .unwrap();
    }
}