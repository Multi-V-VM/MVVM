use std::{fmt, mem};
use zero::{read, read_array, read_str, Pod};
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
    pub input: &'a [u8],
    pub header_part1: &'a HeaderPt1,
    pub header_part2: HeaderPt2<'a>,
    pub section: SectionHeader<'a>,
    pub program: &'a ProgramHeader<'a>,
    pub section_data: &'a SectionData<'a>,
}
impl<'a> ElfFile<'a> {
    pub fn get_shstr(&self, index: u32) -> ParseResult<&'a str> {
        self.get_shstr_table()
            .map(|shstr_table| read_str(&shstr_table[(index as usize)..]))
    }
    fn get_shstr_table(&self) -> ParseResult<&'a [u8]> {
        let header = self
            .parse_section_header(self.input, self.header_part2.get_sh_str_index());
        header.map(|h| &self.input[(h.get_offset() as usize)..])
    }
    pub fn parse_header(&self, input: &'a [u8]) -> ParseResult<(&'a HeaderPt1, HeaderPt2<'a>)> {
        let size_part1 = mem::size_of::<HeaderPt1>();
        if input.len() < size_part1 {
            return Err(ElfError::Malformed(String::from(
                "File is shorter than the first ELF header",
            )));
        }
        let header_part1: &'a HeaderPt1 = read(&input[..size_part1]);
        if header_part1.magic != [0x7f, b'E', b'L', b'F'] {
            ElfError::BadMagic(bytemuck::cast(header_part1.magic));
        }
        let header_part2 = match header_part1.get_class() {
            Class::ThirtyTwo => HeaderPt2::Header32(read(
                &input[size_part1..size_part1 + mem::size_of::<HeaderPt2_<u32>>()],
            )),
            Class::SixtyFour => HeaderPt2::Header64(read(
                &input[size_part1..size_part1 + mem::size_of::<HeaderPt2_<u64>>()],
            )),
            _ => {
                return Err(ElfError::Malformed(String::from("Invalid ELF Class")));
            }
        };
        Ok((header_part1, header_part2))
    }

    pub fn parse_section_header(
        &self,
        input: &'a [u8],
        index: u16,
    ) -> ParseResult<SectionHeader<'a>> {
        /* From index 0 (SHN_UNDEF) is an error */
        let start = (index as u64 * self.header_part2.get_sh_entry_size() as u64
            + self.header_part2.get_sh_offset()) as usize;
        let end = start + self.header_part2.get_sh_entry_size() as usize;
        Ok(match self.header_part1.get_class() {
            Class::ThirtyTwo => {
                let header = read(&input[start..end]);
                SectionHeader::SectionHeader32(header)
            }
            Class::SixtyFour => {
                let header = read(&input[start..end]);
                SectionHeader::SectionHeader64(header)
            }
            Class::None | Class::Other(_) => todo!(),
        })
    }
    pub fn parse_program_header(
        &self,
        input: &'a [u8],
        index: u16,
    ) -> ParseResult<&'a ProgramHeader<'a>> {
        todo!()
    }
    pub fn new(&self, input: &'a [u8]) -> Self {
        let (header_part1, header_part2) = self.parse_header(self.input).unwrap();
        let section = self.parse_section_header(input, 0).unwrap();
        let program = self.parse_program_header(input, 0).unwrap();
        Self {
            input,
            header_part1,
            header_part2,
            section,
            program,
            section_data: todo!(),
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
impl HeaderPt1 {
    pub fn get_class(&self) -> Class {
        match self.class {
            0 => Class::None,
            1 => Class::ThirtyTwo,
            2 => Class::SixtyFour,
            other => Class::Other(other),
        }
    }
    pub fn get_data(&self) -> Data {
        match self.data {
            0 => Data::None,
            1 => Data::LittleEndian,
            2 => Data::BigEndian,
            other => Data::Other(other),
        }
    }
    pub fn get_version(&self) -> Version {
        match self.version {
            0 => Version::None,
            1 => Version::Current,
            other => Version::Other(other),
        }
    }
    pub fn get_os_abi(&self) -> OsAbi {
        match self.os_abi {
            0x00 => OsAbi::SystemV,
            0x01 => OsAbi::HPUX,
            0x02 => OsAbi::NetBSD,
            0x03 => OsAbi::Linux,
            0x04 => OsAbi::GNUHurd,
            0x06 => OsAbi::Solaris,
            0x07 => OsAbi::AIX,
            0x08 => OsAbi::IRIX,
            0x09 => OsAbi::FreeBSD,
            0x0A => OsAbi::Tru64,
            0x0B => OsAbi::NovellModesto,
            0x0C => OsAbi::OpenBSD,
            0x0D => OsAbi::OpenVMS,
            0x0E => OsAbi::NonStopKernel,
            0x0F => OsAbi::AROS,
            0x10 => OsAbi::FenixOS,
            0x11 => OsAbi::NuxiCloudABI,
            0x12 => OsAbi::OpenVOS,
            other => OsAbi::Other(other),
        }
    }
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
    Other(u8),
}
#[derive(Clone, Copy, Debug)]
pub enum HeaderPt2<'a> {
    Header32(&'a HeaderPt2_<u32>),
    Header64(&'a HeaderPt2_<u64>),
}
impl<'a> HeaderPt2<'a> {
    pub fn get_size(&self) -> usize {
        match *self {
            HeaderPt2::Header32(_) => mem::size_of::<HeaderPt2_<u32>>(),
            HeaderPt2::Header64(_) => mem::size_of::<HeaderPt2_<u64>>(),
        }
    }
    pub fn get_type(&self) -> Type {
        match *self {
            HeaderPt2::Header32(h) => match h.type_ {
                0 => Type::None,
                1 => Type::Relocatable,
                2 => Type::Executable,
                3 => Type::SharedObject,
                4 => Type::Core,
                x => Type::ProcessorSpecific(x),
            },
            HeaderPt2::Header64(h) => match h.type_ {
                0 => Type::None,
                1 => Type::Relocatable,
                2 => Type::Executable,
                3 => Type::SharedObject,
                4 => Type::Core,
                x => Type::ProcessorSpecific(x),
            },
        }
    }
    pub fn get_machine(&self) -> Machine {
        match *self {
            HeaderPt2::Header32(h) => match h.type_ {
                0x04 => Machine::MotorolaM68k,
                0x05 => Machine::MotorolaM88k,
                0x06 => Machine::IntelMCU,
                0x07 => Machine::Intel80860,
                0x08 => Machine::MIPS,
                0x09 => Machine::IBMSystem370,
                0x0A => Machine::MIPSRS3000,
                0x0E => Machine::HPRISC,
                0x13 => Machine::Intel80960,
                0x14 => Machine::PowerPC,
                0x15 => Machine::PowerPC64,
                0x16 => Machine::S390,
                0x17 => Machine::IBMSPU,
                0x24 => Machine::NECV800,
                0x25 => Machine::FujitsuFR20,
                0x26 => Machine::TRWRH32,
                0x27 => Machine::MotorolaRCE,
                0x28 => Machine::ARMv7,
                0x29 => Machine::DigitalAlpha,
                0x2A => Machine::SuperH,
                0x2B => Machine::SPARC9,
                0x2C => Machine::SiemensTriCore,
                0x2D => Machine::ArgonautRISC,
                0x2E => Machine::HitachiH8300,
                0x2F => Machine::HitachiH8300H,
                0x30 => Machine::HitachiH8S,
                0x31 => Machine::HitachiH8500,
                0x32 => Machine::IA64,
                0x33 => Machine::MIPS,
                0x34 => Machine::MotorolaColdFire,
                0x35 => Machine::MotorolaM68HC12,
                0x36 => Machine::FujitsuMMA,
                0x37 => Machine::SiemensPCP,
                0x38 => Machine::SonynCPU,
                0x39 => Machine::DensoNDR1,
                0x3A => Machine::MotorolaStar,
                0x3B => Machine::ToyotaME16,
                0x3C => Machine::ST100,
                0x3D => Machine::TinyJ,
                0x3E => Machine::AMDx64,
                0x8C => Machine::TMS320C6000,
                0xAF => Machine::MCSTElbrus,
                0xB7 => Machine::ARMv8,
                0xF3 => Machine::RISCV,
                0xF7 => Machine::BPF,
                0x101 => Machine::WDC65C816,
                other => Machine::Other(other),
            },
            HeaderPt2::Header64(h) => match h.type_ {
                0x04 => Machine::MotorolaM68k,
                0x05 => Machine::MotorolaM88k,
                0x06 => Machine::IntelMCU,
                0x07 => Machine::Intel80860,
                0x08 => Machine::MIPS,
                0x09 => Machine::IBMSystem370,
                0x0A => Machine::MIPSRS3000,
                0x0E => Machine::HPRISC,
                0x13 => Machine::Intel80960,
                0x14 => Machine::PowerPC,
                0x15 => Machine::PowerPC64,
                0x16 => Machine::S390,
                0x17 => Machine::IBMSPU,
                0x24 => Machine::NECV800,
                0x25 => Machine::FujitsuFR20,
                0x26 => Machine::TRWRH32,
                0x27 => Machine::MotorolaRCE,
                0x28 => Machine::ARMv7,
                0x29 => Machine::DigitalAlpha,
                0x2A => Machine::SuperH,
                0x2B => Machine::SPARC9,
                0x2C => Machine::SiemensTriCore,
                0x2D => Machine::ArgonautRISC,
                0x2E => Machine::HitachiH8300,
                0x2F => Machine::HitachiH8300H,
                0x30 => Machine::HitachiH8S,
                0x31 => Machine::HitachiH8500,
                0x32 => Machine::IA64,
                0x33 => Machine::MIPS,
                0x34 => Machine::MotorolaColdFire,
                0x35 => Machine::MotorolaM68HC12,
                0x36 => Machine::FujitsuMMA,
                0x37 => Machine::SiemensPCP,
                0x38 => Machine::SonynCPU,
                0x39 => Machine::DensoNDR1,
                0x3A => Machine::MotorolaStar,
                0x3B => Machine::ToyotaME16,
                0x3C => Machine::ST100,
                0x3D => Machine::TinyJ,
                0x3E => Machine::AMDx64,
                0x8C => Machine::TMS320C6000,
                0xAF => Machine::MCSTElbrus,
                0xB7 => Machine::ARMv8,
                0xF3 => Machine::RISCV,
                0xF7 => Machine::BPF,
                0x101 => Machine::WDC65C816,
                other => Machine::Other(other),
            },
        }
    }
    fn get_version(&self) -> u32 {
        match *self {
            HeaderPt2::Header32(h) => h.version,
            HeaderPt2::Header64(h) => h.version,
        }
    }
    fn get_entry_point(&self) -> u64 {
        match *self {
            HeaderPt2::Header32(h) => h.entry_point as u64,
            HeaderPt2::Header64(h) => h.entry_point,
        }
    }
    fn get_ph_offset(&self) -> u64 {
        match *self {
            HeaderPt2::Header32(h) => h.ph_offset as u64,
            HeaderPt2::Header64(h) => h.ph_offset,
        }
    }
    fn get_sh_offset(&self) -> u64 {
        match *self {
            HeaderPt2::Header32(h) => h.sh_offset as u64,
            HeaderPt2::Header64(h) => h.sh_offset,
        }
    }
    fn get_flags(&self) -> u32 {
        match *self {
            HeaderPt2::Header32(h) => h.flags,
            HeaderPt2::Header64(h) => h.flags,
        }
    }
    fn get_header_size(&self) -> u16 {
        match *self {
            HeaderPt2::Header32(h) => h.header_size,
            HeaderPt2::Header64(h) => h.header_size,
        }
    }
    fn get_ph_entry_size(&self) -> u16 {
        match *self {
            HeaderPt2::Header32(h) => h.ph_entry_size,
            HeaderPt2::Header64(h) => h.ph_entry_size,
        }
    }
    fn get_ph_count(&self) -> u16 {
        match *self {
            HeaderPt2::Header32(h) => h.ph_count,
            HeaderPt2::Header64(h) => h.ph_count,
        }
    }
    fn get_sh_entry_size(&self) -> u16 {
        match *self {
            HeaderPt2::Header32(h) => h.sh_entry_size,
            HeaderPt2::Header64(h) => h.sh_entry_size,
        }
    }
    fn get_sh_str_index(&self) -> u16 {
        match *self {
            HeaderPt2::Header32(h) => h.sh_str_index,
            HeaderPt2::Header64(h) => h.sh_str_index,
        }
    }
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
    ProcessorSpecific(u16),
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

#[derive(Debug, Clone,PartialEq,Eq,Copy)]
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
    Group,
    SymTabShIndex,
    OsSpecific(u32),
    ProcessorSpecific(u32),
    User(u32),
}

#[derive(Debug, Clone)]
pub enum SectionHeader<'a> {
    SectionHeader32(&'a SectionHeader_<u32>),
    SectionHeader64(&'a SectionHeader_<u64>),
}

impl<'a> SectionHeader<'a> {
    // Note that this function is O(n) in the length of the name.
    pub fn get_name(&self, elf_file: &ElfFile<'a>) -> ParseResult<&'a str> {
        self.get_type().and_then(|typ| match typ {
            SectionHeaderType::NOTE => Err(ElfError::Malformed(String::from(
                "Attempt to get name of null section",
            ))),
            _ => elf_file.get_shstr(self.get_name_()),
        })
    }

    pub fn get_type(&self) -> ParseResult<SectionHeaderType> {
        Ok(self.get_section_type())
    }

    pub fn get_data(&self, elf_file: &ElfFile<'a>) -> ParseResult<SectionData<'a>> {
        macro_rules! array_data {
            ($data32: ident, $data64: ident) => {{
                let data = self.raw_data(elf_file);
                match elf_file.header_part1.get_class() {
                    Class::ThirtyTwo => SectionData::$data32(read_array(data)),
                    Class::SixtyFour => SectionData::$data64(read_array(data)),
                    Class::None | Class::Other(_) => unreachable!(),
                }
            }};
        }

        self.get_type().map(|typ| match typ {
            _ | SectionHeaderType::NOTE | SectionHeaderType::NoBits => SectionData::Empty,
            SectionHeaderType::ProgramBits
            | SectionHeaderType::SharedLibrary
            | SectionHeaderType::OsSpecific(_)
            | SectionHeaderType::ProcessorSpecific(_)
            | SectionHeaderType::User(_) => SectionData::Undefined(self.raw_data(elf_file)),
            SectionHeaderType::SymbolTable => array_data!(SymbolTable32, SymbolTable64),
            SectionHeaderType::DynamicSymbolTable => {
                array_data!(DynSymbolTable32, DynSymbolTable64)
            }
            SectionHeaderType::StringTable => SectionData::StrArray(self.raw_data(elf_file)),
            SectionHeaderType::InitializeArray
            | SectionHeaderType::TerminationArray
            | SectionHeaderType::PreInitializeArray => {
                array_data!(FnArray32, FnArray64)
            }
            SectionHeaderType::RelocationAddendTable => array_data!(Rela32, Rela64),
            SectionHeaderType::RelocationAddendTable => array_data!(Rel32, Rel64),
            SectionHeaderType::DynamicLinkingTable => array_data!(Dynamic32, Dynamic64),
            SectionHeaderType::Group => {
                let data = self.raw_data(elf_file);
                unsafe {
                    let flags: &'a u32 = mem::transmute(&data[0]);
                    let indicies: &'a [u32] = read_array(&data[4..]);
                    SectionData::Group {
                        flags: flags,
                        indicies: indicies,
                    }
                }
            }
            SectionHeaderType::SymTabShIndex => {
                SectionData::SymTabShIndex(read_array(self.raw_data(elf_file)))
            }
            SectionHeaderType::NOTE => {
                let data = self.raw_data(elf_file);
                match elf_file.header_part1.get_class() {
                    Class::ThirtyTwo => unimplemented!(),
                    Class::SixtyFour => {
                        let header: &'a NoteHeader = read(&data[0..12]);
                        let index = &data[12..];
                        SectionData::Note64(header, index)
                    }
                    Class::None | Class::Other(_) => unreachable!(),
                }
            }
            SectionHeaderType::HashTable => {
                let data = self.raw_data(elf_file);
                SectionData::HashTable(read(&data[0..12]))
            }
        })
    }
    pub fn raw_data(&self, elf_file: &ElfFile<'a>) -> &'a [u8] {
        assert_ne!(self.get_section_type(), SectionHeaderType::NOTE);
        &elf_file.input[self.get_offset() as usize..(self.get_offset() + self.get_size()) as usize]
    }
    fn get_flags(&self)->u64{
        match *self {
            SectionHeader::SectionHeader32(h) => h.flags as u64,
            SectionHeader::SectionHeader64(h) => h.flags,
        }
    }
    fn get_name_ (&self)->u32{
        match *self {
            SectionHeader::SectionHeader32(h) => h.name,
            SectionHeader::SectionHeader64(h) => h.name,
        }
    }
    fn get_address(&self)->u64{
        match *self{
            SectionHeader::SectionHeader32(h) => h.address as u64,
            SectionHeader::SectionHeader64(h) => h.address,
        }
    }
    fn get_offset(&self)->u64{
        match *self{
            SectionHeader::SectionHeader32(h) => h.offset as u64,
            SectionHeader::SectionHeader64(h) => h.offset,
        }
    }
    fn get_size(&self)->u64{
        match *self{
            SectionHeader::SectionHeader32(h) => h.size as u64,
            SectionHeader::SectionHeader64(h) => h.size,
        }
    }
    fn get_section_type(&self)->SectionHeaderType{
        match *self{
            SectionHeader::SectionHeader32(h) => h.section_type,
            SectionHeader::SectionHeader64(h) => h.section_type,
        }
    }
    fn get_link(&self)->u32{
        match *self{
            SectionHeader::SectionHeader32(h) => h.link ,
            SectionHeader::SectionHeader64(h) => h.link,
        }
    }
    fn get_info(&self)->u32{
        match *self{
            SectionHeader::SectionHeader32(h) => h.info ,
            SectionHeader::SectionHeader64(h) => h.info,
        }
    }

}

#[derive(Debug, Clone,Copy)]
pub struct SectionHeader_<P> {
    ///	contains the offset, in bytes, to the section name, relative to the start of the section
    /// name string table.
    name: u32,
    /// identifies the section type.
    section_type: SectionHeaderType,
    /// identifies the attributes of the section.
    flags: P,
    /// contains the virtual address of the beginning of the section in memory. If the section is
    /// not allocated to the memory image of the program, this field should be zero.
    address: P,
    /// contains the offset, in bytes, of the beginning of the section contents in the file.
    offset: P,
    /// contains the size, in bytes, of the section. Except for ShtNoBits sections, this is the
    /// amount of space occupied in the file.
    size: P,
    /// contains the section index of an associated section. This field is used for several
    /// purposes, depending on the type of section, as explained in Table 10.
    link: u32,
    /// contains extra information about the section. This field is used for several purposes,
    /// depending on the type of section, as explained in Table 11.
    info: u32,
    /// contains the required alignment of the section. This field must be a power of two.
    alignment: P,
    /// contains the size, in bytes, of each entry, for sections that contain fixed-size entries.
    /// Otherwise, this field contains zero.
    entry_size: P,
}

impl<P> SectionHeader_<P> {
}

unsafe impl<P> Pod for SectionHeader_<P> {}

#[derive(Debug)]
pub enum SectionData<'a> {
    Empty,
    Undefined(&'a [u8]),
    Group { flags: &'a u32, indicies: &'a [u32] },
    StrArray(&'a [u8]),
    FnArray32(&'a [u32]),
    FnArray64(&'a [u64]),
    SymbolTable32(&'a [Entry32]),
    SymbolTable64(&'a [Entry64]),
    DynSymbolTable32(&'a [DynEntry32]),
    DynSymbolTable64(&'a [DynEntry64]),
    SymTabShIndex(&'a [u32]),
    // Note32 uses 4-byte words, which I'm not sure how to manage.
    // The pointer is to the start of the name field in the note.
    Note64(&'a NoteHeader, &'a [u8]),
    Rela32(&'a [Rela<u32>]),
    Rela64(&'a [Rela<u64>]),
    Rel32(&'a [Rel<u32>]),
    Rel64(&'a [Rel<u64>]),
    Dynamic32(&'a [Dynamic<u32>]),
    Dynamic64(&'a [Dynamic<u64>]),
    HashTable(&'a HashTable),
}

#[derive(Debug)]
#[repr(C)]
pub struct Dynamic<P>
where
    Tag_<P>: fmt::Debug,
{
    tag: Tag_<P>,
    un: P,
}

unsafe impl<P> Pod for Dynamic<P> where Tag_<P>: fmt::Debug {}

#[derive(Copy, Clone, Debug)]
pub struct Tag_<P>(P);

#[derive(Debug, PartialEq, Eq)]
pub enum Tag<P> {
    Null,
    Needed,
    PltRelSize,
    Pltgot,
    Hash,
    StrTab,
    SymTab,
    Rela,
    RelaSize,
    RelaEnt,
    StrSize,
    SymEnt,
    Init,
    Fini,
    SoName,
    RPath,
    Symbolic,
    Rel,
    RelSize,
    RelEnt,
    PltRel,
    Debug,
    TextRel,
    JmpRel,
    BindNow,
    InitArray,
    FiniArray,
    InitArraySize,
    FiniArraySize,
    RunPath,
    Flags,
    PreInitArray,
    PreInitArraySize,
    SymTabShIndex,
    Flags1,
    OsSpecific(P),
    ProcessorSpecific(P),
}

#[derive(Debug)]
#[repr(C)]
pub struct Rela<P> {
    offset: P,
    info: P,
    addend: P,
}

#[derive(Debug)]
#[repr(C)]
pub struct Rel<P> {
    offset: P,
    info: P,
}

unsafe impl<P> Pod for Rela<P> {}
unsafe impl<P> Pod for Rel<P> {}

#[derive(Debug)]
#[repr(C)]
struct Entry32_ {
    name: u32,
    value: u32,
    size: u32,
    info: u8,
    other: u8,
    shndx: u16,
}

#[derive(Debug)]
#[repr(C)]
struct Entry64_ {
    name: u32,
    info: u8,
    other: u8,
    shndx: u16,
    value: u64,
    size: u64,
}

unsafe impl Pod for Entry32_ {}
unsafe impl Pod for Entry64_ {}

#[derive(Debug)]
#[repr(C)]
pub struct Entry32(Entry32_);

#[derive(Debug)]
#[repr(C)]
pub struct Entry64(Entry64_);

unsafe impl Pod for Entry32 {}
unsafe impl Pod for Entry64 {}

#[derive(Debug)]
#[repr(C)]
pub struct DynEntry32(Entry32_);

#[derive(Debug)]
#[repr(C)]
pub struct DynEntry64(Entry64_);

unsafe impl Pod for DynEntry32 {}
unsafe impl Pod for DynEntry64 {}

#[derive(Copy, Clone, Debug)]
#[repr(u8)]
pub enum Visibility {
    Default = 0,
    Internal = 1,
    Hidden = 2,
    Protected = 3,
}
#[derive(Copy, Clone, Debug)]
#[repr(C)]
pub struct NoteHeader {
    name_size: u32,
    desc_size: u32,
    type_: u32,
}
unsafe impl Pod for NoteHeader {}
#[derive(Clone, Copy, Debug)]
#[repr(C)]
pub struct HashTable {
    bucket_count: u32,
    chain_count: u32,
    first_bucket: u32,
}

unsafe impl Pod for HashTable {}
#[derive(Debug, Clone)]
pub enum ProgramHeader<'a> {
    ProgramHeader32(&'a ProgramHeader32),
    ProgramHeader64(&'a ProgramHeader64),
}

#[derive(Copy, Clone, Debug, Default)]
#[repr(C)]
pub struct ProgramHeader32 {
    pub type_: u32,
    pub offset: u32,
    pub virtual_addr: u32,
    pub physical_addr: u32,
    pub file_size: u32,
    pub mem_size: u32,
    pub flags: u32,
    pub align: u32,
}

#[derive(Copy, Clone, Debug, Default)]
#[repr(C)]
pub struct ProgramHeader64 {
    pub type_: u32,
    pub flags: u32,
    pub offset: u64,
    pub virtual_addr: u64,
    pub physical_addr: u64,
    pub file_size: u64,
    pub mem_size: u64,
    pub align: u64,
}
unsafe impl Pod for ProgramHeader32 {}
unsafe impl Pod for ProgramHeader64 {}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum ProgramHeaderType {
    Null,
    Load,
    Dynamic,
    Interp,
    Note,
    ShLib,
    Phdr,
    Tls,
    GnuRelro,
    OsSpecific(u32),
    ProcessorSpecific(u32),
}
