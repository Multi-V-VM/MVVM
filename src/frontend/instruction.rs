use std::ops::RangeInclusive;

use super::page::Page;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ISABase {
    RV32,
    RV64,
    RV128,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ISAExtension {
    I,
    M,
    A,
    F,
    D,
    Q,
    C,
    V,
    B,
    Zifencei,
    Zicsr,
}
#[derive(Debug, Clone)]
pub struct ISA {
    base: ISABase,
    ext: ISAExtension,
}

impl ISABase {
    fn get_name(self) -> &'static str {
        match self {
            Self::RV32 => "RV32",
            Self::RV64 => "RV64",
            Self::RV128 => "RV128",
        }
    }
}
impl ISAExtension {
    fn get_name(self) -> &'static str {
        match self {
            Self::I => "I",
            Self::M => "M",
            Self::A => "A",
            Self::F => "F",
            Self::D => "D",
            Self::Q => "Q",
            Self::V => "V",
            Self::B => "B",
            Self::C => "C",
            Self::Zifencei => "Zifencei",
            Self::Zicsr => "Zicsr",
        }
    }
}
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct RType(pub u32);
impl RType {
    pub fn rs2(&self) -> u32 {
        (self.0 >> 20) & 0x1f
    }
    pub fn rs1(&self) -> u32 {
        (self.0 >> 15) & 0x1f
    }
    pub fn rd(&self) -> u32 {
        (self.0 >> 7) & 0x1f
    }
}
#[derive(Debug, Clone)]
pub struct InstructionName {
    pub pos: usize,
    pub name: String,
}

#[derive(Debug, Clone)]
pub struct InstructionField {
    pub instruction_bit_range: InstBitRange,
    // pub field_def: FieldDef,
}
#[derive(Debug, Clone)]
pub struct InstBitRange {
    pub bits: RangeInclusive<u8>,
    pub end_bit_pos: usize,
    pub start_bit_pos: usize,
}
#[derive(Debug, Clone)]
pub struct Instruction {
    pub isa: ISA,
    pub name: InstructionName,
    pub fields: Vec<InstructionField>,
}

#[derive(Debug, Clone)]
pub struct InstructionIter<'a> {
    pub address: u64,
    pub memory_map: &'a Vec<Option<Page<'a>>>,
}

impl Iterator for InstructionIter<'_> {
    type Item = (u64, Instruction);

    fn next(&mut self) -> Option<Self::Item> {
        todo!()
    }
}
