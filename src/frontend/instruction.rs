use std::ops::RangeInclusive;

use super::page::Page;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ISABase {
    RV32,
    RV64,
    RV128,
}
/// Rd
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Rd(pub Reg);
/// Rs
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Rs(pub Reg);
/// Rs1
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Rs1(pub Reg);
/// Rs2
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Rs2(pub Reg);
/// Rs3
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Rs3(pub Reg);
/// Atomic instruction flag: Acquire
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct AQ(pub bool);
/// Atomic instruction flag: Release
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct RL(pub bool);

/// Shift-amount
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct Shamt(pub u8);

/// Xx From X0-X31 or X63
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Xx<const MAX: u32>(u32, char);

impl<const MAX: u32> Xx<MAX> {
    pub fn new(value: u32, prefix: char) -> Self {
        Self(value, prefix)
    }
}
impl<const MAX: u32> Into<String> for Xx<MAX> {
    fn into(self) -> String {
        format!("{}{}", self.0, self.1)
    }
}
/// Reg
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Reg {
    X(Xx<32>),
    F(Xx<32>),
    PC,
    FCSR,
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
impl Instruction {
    fn parse_instruction() -> Instruction {
        Instruction {
            isa: ISA {
                base: ISABase::RV32,
                ext: ISAExtension::I,
            },
            name: InstructionName {
                pos: 0,
                name: "".to_string(),
            },
            fields: vec![],
        }
    }
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
