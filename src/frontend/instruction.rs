use std::ops::RangeInclusive;

use super::page::Page;
#[allow(non_camel_case_types)]
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
/// Floating-point rounding mode
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum RoundingMode {
    /// Round to nearest, ties to even
    RNE = 0b000,
    /// Round towards zero
    RTZ = 0b001,
    /// Round towards -infinity
    RDN = 0b010,
    /// Round towards +infinity
    RUP = 0b011,
    /// Round to nearest, ties to max magnitude
    RMM = 0b100,
    /// In instruction's rm field, select dynamic rounding mode;
    /// In Rounding Mode register, reserved.
    DYN = 0b111,
}
#[derive(Clone, Copy, Eq, Debug, PartialEq)]
pub struct Imm32<const HIGH_BIT: usize, const LOW_BIT: usize>(pub u32);
impl<const HIGH_BIT: usize, const LOW_BIT: usize> Imm32<HIGH_BIT, LOW_BIT> {
    pub fn new(value: u32) -> Self {
        Self(value)
    }
    pub fn from(underlying: u32) -> Self {
        Self::new(underlying)
    }
    pub fn valib_bits(&self) -> usize {
        HIGH_BIT - LOW_BIT + 1
    }
    /// Decode the immediate value by placing its valid bits at the range of [HIGH_BIT, LOW_BIT] accourding to the spec of RISCV
    pub fn decode(self) -> u32 {
        let mut res = self.0;
        res <<= 32 - HIGH_BIT - 1;
        res >>= 32 - HIGH_BIT + LOW_BIT - 1;
        res
    }
    pub fn decode_sext(self) -> i32 {
        todo!()
    }
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
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum RV32I {
    LUI(Rd, Imm32<31, 12>),
    AUIPC(Rd, Imm32<31, 12>),
    JAL(Rd, Imm32<31, 12>),
    JALR(Rd, Rs, Imm32<31, 12>),
    BEQ(Rs1, Rs2, Imm32<31, 12>),
    BNE(Rs1, Rs2, Imm32<31, 12>),
    BLT(Rs1, Rs2, Imm32<12, 1>),
    BGE(Rs1, Rs2, Imm32<12, 1>),
    BLTU(Rs1, Rs2, Imm32<12, 1>),
    BGEU(Rs1, Rs2, Imm32<12, 1>),
    LB(Rd, Rs1, Imm32<11, 0>),
    LH(Rd, Rs1, Imm32<11, 0>),
    LW(Rd, Rs1, Imm32<11, 0>),
    LBU(Rd, Rs1, Imm32<11, 0>),
    LHU(Rd, Rs1, Imm32<11, 0>),
    SB(Rs1, Rs2, Imm32<11, 0>),
    SH(Rs1, Rs2, Imm32<11, 0>),
    SW(Rs1, Rs2, Imm32<11, 0>),
    ADDI(Rd, Rs1, Imm32<11, 0>),
    SLTI(Rd, Rs1, Imm32<11, 0>),
    SLTIU(Rd, Rs1, Imm32<11, 0>),
    XORI(Rd, Rs1, Imm32<11, 0>),
    ORI(Rd, Rs1, Imm32<11, 0>),
    ANDI(Rd, Rs1, Imm32<11, 0>),
    SLLI(Rd, Rs1, Shamt),
    SRLI(Rd, Rs1, Shamt),
    SRAI(Rd, Rs1, Shamt),
    ADD(Rd, Rs1, Rs2),
    SUB(Rd, Rs1, Rs2),
    SLL(Rd, Rs1, Rs2),
    SLT(Rd, Rs1, Rs2),
    SLTU(Rd, Rs1, Rs2),
    XOR(Rd, Rs1, Rs2),
    SRL(Rd, Rs1, Rs2),
    SRA(Rd, Rs1, Rs2),
    OR(Rd, Rs1, Rs2),
    AND(Rd, Rs1, Rs2),
    FENCE(Rd, Rs1, FenceSucc, FencePred, FenceFm),
    FENCE_TSO,
    PAUSE,
    ECALL,
    EBREAK,
}
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum RV32M {
    MUL(Rd, Rs1, Rs2),
    MULH(Rd, Rs1, Rs2),
    MULHSU(Rd, Rs1, Rs2),
    MULHU(Rd, Rs1, Rs2),
    DIV(Rd, Rs1, Rs2),
    DIVU(Rd, Rs1, Rs2),
    REM(Rd, Rs1, Rs2),
    REMU(Rd, Rs1, Rs2),
}
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum RV32A {
    LR_W(Rd, Rs1, AQ, RL),
    SC_W(Rd, Rs1, Rs2, AQ, RL),
    AMOSWAP_W(Rd, Rs1, Rs2, AQ, RL),
    AMOADD_W(Rd, Rs1, Rs2, AQ, RL),
    AMOXOR_W(Rd, Rs1, Rs2, AQ, RL),
    AMOAND_W(Rd, Rs1, Rs2, AQ, RL),
    AMOOR_W(Rd, Rs1, Rs2, AQ, RL),
    AMOMIN_W(Rd, Rs1, Rs2, AQ, RL),
    AMOMAX_W(Rd, Rs1, Rs2, AQ, RL),
    AMOMINU_W(Rd, Rs1, Rs2, AQ, RL),
    AMOMAXU_W(Rd, Rs1, Rs2, AQ, RL),
}
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum RV32F {
    FLW(Rd, Rs1, Imm32<11, 0>),
    FSW(Rs1, Rs2, Imm32<11, 0>),
    FMADD_S(Rd, Rs1, Rs2, Rs3, RoundingMode),
    FMSUB_S(Rd, Rs1, Rs2, Rs3, RoundingMode),
    FNMSUB_S(Rd, Rs1, Rs2, Rs3, RoundingMode),
    FNMADD_S(Rd, Rs1, Rs2, Rs3, RoundingMode),
    FADD_S(Rd, Rs1, Rs2, RoundingMode),
    FSUB_S(Rd, Rs1, Rs2, RoundingMode),
    FMUL_S(Rd, Rs1, Rs2, RoundingMode),
    FDIV_S(Rd, Rs1, Rs2, RoundingMode),
    FSQRT_S(Rd, Rs1, RoundingMode),
    FSGNJ_S(Rd, Rs1, Rs2),
    FSGNJN_S(Rd, Rs1, Rs2),
    FSGNJX_S(Rd, Rs1, Rs2),
    FMIN_S(Rd, Rs1, Rs2),
    FMAX_S(Rd, Rs1, Rs2),
    FCVT_W_S(Rd, Rs1, RoundingMode),
    FCVT_WU_S(Rd, Rs1, RoundingMode),
    FMV_X_W(Rd, Rs1),
    FEQ_S(Rd, Rs1, Rs2),
    FLT_S(Rd, Rs1, Rs2),
    FLE_S(Rd, Rs1, Rs2),
    FCLASS_S(Rd, Rs1),
    FCVT_S_W(Rd, Rs1, RoundingMode),
    FCVT_S_WU(Rd, Rs1, RoundingMode),
    FMV_W_X(Rd, Rs1),
}
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum RV32D {
    FLD(Rd, Rs1, Imm32<11, 0>),
    FSD(Rs1, Rs2, Imm32<11, 0>),
    FMADD_D(Rd, Rs1, Rs2, Rs3, RoundingMode),
    FMSUB_D(Rd, Rs1, Rs2, Rs3, RoundingMode),
    FNMSUB_D(Rd, Rs1, Rs2, Rs3, RoundingMode),
    FNMADD_D(Rd, Rs1, Rs2, Rs3, RoundingMode),
    FADD_D(Rd, Rs1, Rs2, RoundingMode),
    FSUB_D(Rd, Rs1, Rs2, RoundingMode),
    FMUL_D(Rd, Rs1, Rs2, RoundingMode),
    FDIV_D(Rd, Rs1, Rs2, RoundingMode),
    FSQRT_D(Rd, Rs1, RoundingMode),
    FSGNJ_D(Rd, Rs1, Rs2),
    FSGNJN_D(Rd, Rs1, Rs2),
    FSGNJX_D(Rd, Rs1, Rs2),
    FMIN_D(Rd, Rs1, Rs2),
    FMAX_D(Rd, Rs1, Rs2),
    FCVT_S_D(Rd, Rs1, RoundingMode),
    FCVT_D_S(Rd, Rs1, RoundingMode),
    FEQ_D(Rd, Rs1, Rs2),
    FLT_D(Rd, Rs1, Rs2),
    FLE_D(Rd, Rs1, Rs2),
    FCLASS_D(Rd, Rs1),
    FCVT_W_D(Rd, Rs1, RoundingMode),
    FCVT_WU_D(Rd, Rs1, RoundingMode),
    FCVT_D_W(Rd, Rs1, RoundingMode),
    FCVT_D_WU(Rd, Rs1, RoundingMode),
}
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum RV32V {}
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct FenceFm(pub Xx<16>);
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct FencePred(pub Xx<16>);
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct FenceSucc(pub Xx<16>);

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
