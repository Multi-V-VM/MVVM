use std::mem;
use zero::{read, Pod};
#[derive(Debug)]
pub enum ElfError {
    /// The Binary is Malformed SomeWhere
    Malformed(String),
    /// The Magic is Unknown
    BadMagic(u64),
    /// An IO based error
    #[cfg(feature = "std")]
    IO(io::Error),
    /// Possible Out of User Space Bound Mapping
    AddressError(u64, String),
}

impl From<u64> for ElfError {
    fn from(e: u64) -> Self {
        Self::BadMagic(e)
    }
}
impl From<String> for ElfError {
    fn from(e: String) -> Self {
        Self::Malformed(e)
    }
}
impl From<(u64, String)> for ElfError {
    fn from(e: (u64, String)) -> Self {
        Self::AddressError(e.0, e.1)
    }
}

pub type ParseResult<T> = Result<T, ElfError>;

#[derive(Debug, Clone)]
pub struct ElfFile<'a> {
    pub header_part1: &'a HeaderPt1,
    pub header_part2: HeaderPt2<'a>,
    pub section: &'a SectionHeader,
    pub program: &'a ProgramHeader,
}
impl<'a> ElfFile<'a> {
    pub fn parse_header(input: &'a [u8]) -> ParseResult<(&'a HeaderPt1, HeaderPt2<'a>)> {
        let size_part1 = mem::size_of::<HeaderPt1>();
        if input.len() < size_part1 {
            return Err(ElfError::Malformed(String::from("File is shorter than the first ELF header")));
        }
        let header_part1 : &'a HeaderPt1 = read(&input[..size_part1]);
        if header_part1.magic != [0x7f, b'E', b'L', b'F'] {
            ElfError::BadMagic(bytemuck::cast(header_part1.magic));
        }
        let header_part2 = match header_part1.class{
            1=> {
                HeaderPt2::Header32(read(&input[size_part1..size_part1 + mem::size_of::<HeaderPt2_<u32>>()]))
            }
            2=>{
                HeaderPt2::Header64(read(&input[size_part1..size_part1 + mem::size_of::<HeaderPt2_<u64>>()]))
            }
            _=>{
                return Err(ElfError::Malformed(String::from("Invalid ELF Class")));
            }
        };
        Ok((header_part1, header_part2))
    }
}
#[derive(Clone, Copy, Debug)]
pub enum HeaderPt2<'a> {
    Header32(&'a HeaderPt2_<u32>),
    Header64(&'a HeaderPt2_<u64>),
}
impl<'a> HeaderPt2<'a> {
    pub fn size(&self) -> usize {
        match *self {
            HeaderPt2::Header32(_) => mem::size_of::<HeaderPt2_<u32>>(),
            HeaderPt2::Header64(_) => mem::size_of::<HeaderPt2_<u64>>(),
        }
    }
}

#[derive(Copy, Clone, Debug)]
#[repr(C)]
pub struct HeaderPt1 {
    pub magic: [u8; 4],
    pub class: u8,
    pub data: u8,
    pub version: u8,
    pub os_abi: u8,
    // Often also just padding.
    pub abi_version: u8,
    pub padding: [u8; 7],
}
unsafe impl Pod for HeaderPt1 {}
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum Class {
    None,
    ThirtyTwo,
    SixtyFour,
    Other(u8),
}
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum Data {
    None,
    LittleEndian,
    BigEndian,
    Other(u8),
}
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum Version {
    None,
    Current,
    Other(u8),
}
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum OsAbi {
    SystemV,
    HPUX,
    NetBSD,
    Linux,
    GNUHurd,
    Solaris,
    AIX,
    IRIX,
    FreeBSD,
    Tru64,
    NovellModesto,
    OpenBSD,
    OpenVMS,
    NonStopKernel,
    AROS,
    FenixOS,
    NuxiCloudABI,
    OpenVOS,
}
#[derive(Debug)]
#[repr(C)]
pub struct HeaderPt2_<P> {
    pub type_: u16,
    pub machine: u16,
    pub version: u32,
    pub entry_point: P,
    pub ph_offset: P,
    pub sh_offset: P,
    pub flags: u32,
    pub header_size: u16,
    pub ph_entry_size: u16,
    pub ph_count: u16,
    pub sh_entry_size: u16,
    pub sh_count: u16,
    pub sh_str_index: u16,
}
unsafe impl<P> Pod for HeaderPt2_<P> {}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum Type {
    None,
    Relocatable,
    Executable,
    SharedObject,
    Core,
    ProcessorSpecific(u16), // TODO OsSpecific
}
#[allow(non_camel_case_types)]
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum Machine {
    None,
    ATAT,
    Sparc,
    X86,
    MotorolaM68k,
    MotorolaM88k,
    IntelMCU,
    Intel80860,
    MIPS,
    IBMSystem370,
    MIPSRS3000,
    HPRISC,
    Intel80960,
    PowerPC,
    PowerPC64,
    S390,
    IBMSPU,
    NECV800,
    FujitsuFR20,
    TRWRH32,
    MotorolaRCE,
    ARMv7,
    DigitalAlpha,
    SuperH,
    SPARC9,
    SiemensTriCore,
    ArgonautRISC,
    HitachiH8300,
    HitachiH8300H,
    HitachiH8S,
    HitachiH8500,
    IA64,
    MIPSX,
    MotorolaColdFire,
    MotorolaM68HC12,
    FujitsuMMA,
    SiemensPCP,
    SonynCPU,
    DensoNDR1,
    MotorolaStar,
    ToyotaME16,
    ST100,
    TinyJ,
    AMDx64,
    TMS320C6000,
    MCSTElbrus,
    ARMv8,
    RISCV,
    BPF,
    WDC65C816,
    Other(u16),
}

#[derive(Debug, Clone)]
pub enum SectionHeaderType {
    /// marks an unused section header
    SectionNull,
    /// information defined by the program
    ProgramBits,
    /// a linker symbol table
    SymbolTable,
    /// a string table
    StringTable,
    /// “Rela” type relocation entries
    RelocationAddendTable,
    /// a symbol hash table
    HashTable,
    /// dynamic linking tables
    DynamicLinkingTable,
    /// note information
    NOTE,
    /// uninitialized space; does not occupy any space in the file
    NoBits,
    /// “Rel” type relocation entries
    RelocationTable,
    /// reserved
    SharedLibrary,
    /// a dynamic loader symbol table
    DynamicSymbolTable,
    /// an array of pointers to initialization functions
    InitializeArray,
    /// an array of pointers to termination functions
    TerminationArray,
    /// an array of pointers to pre-initialization functions
    PreInitializeArray,
}

#[derive(Debug, Clone)]
pub struct SectionHeader {
    ///	contains the offset, in bytes, to the section name, relative to the start of the section
    /// name string table.
    name: u32,
    /// identifies the section type.
    section_type: SectionHeaderType,
    /// identifies the attributes of the section.
    flags: usize,
    /// contains the virtual address of the beginning of the section in memory. If the section is
    /// not allocated to the memory image of the program, this field should be zero.
    address: usize,
    /// contains the offset, in bytes, of the beginning of the section contents in the file.
    offset: usize,
    /// contains the size, in bytes, of the section. Except for ShtNoBits sections, this is the
    /// amount of space occupied in the file.
    size: usize,
    /// contains the section index of an associated section. This field is used for several
    /// purposes, depending on the type of section, as explained in Table 10.
    link: u32,
    /// contains extra information about the section. This field is used for several purposes,
    /// depending on the type of section, as explained in Table 11.
    info: u32,
    /// contains the required alignment of the section. This field must be a power of two.
    alignment: usize,
    /// contains the size, in bytes, of each entry, for sections that contain fixed-size entries.
    /// Otherwise, this field contains zero.
    entry_size: usize,
}

impl SectionHeader {
    const WRITE: usize = 1;
    const ALLOCATE: usize = 2;
    const EXECUTABLE: usize = 4;
    const fn is_write(self: SectionHeader) -> bool {
        (self.flags & Self::WRITE) > 0
    }
    const fn is_allocate(self: SectionHeader) -> bool {
        (self.flags & Self::ALLOCATE) > 0
    }
    const fn is_excutable(self: SectionHeader) -> bool {
        (self.flags & Self::EXECUTABLE) > 0
    }
    pub fn get_Name(&self, elf_file: &ElfFile<'_>) -> ParseResult<& str> {
        todo!()
    }
}
#[derive(Debug, Clone)]
pub struct ProgramHeader {}
