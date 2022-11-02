use super::page::{Page, PageIndexOfs};
use std::ops::RangeInclusive;

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
/// Vmask
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VM(pub bool);
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
    V(Xx<32>),
    PC,
    FCSR,
}

// impl Into<i32> for Reg{
//     fn into(self) -> i32{
//         match self {
//             Reg::X(x) => x.0 as i32,
//             Reg::F(x) => x.0 as i32,
//             Reg::V(x) => x.0 as i32,
//             Reg::PC => 0,
//             Reg::FCSR => 0,
//         }
// }

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ISAExtension {
    I,
    M,
    A,
    F,
    E,
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
        ((self.decode() << (31 - HIGH_BIT)) as i32) >> (31 - HIGH_BIT)
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
            Self::E => "E",
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
#[allow(non_camel_case_types)]
pub enum OpCode {
    LOAD = 0b00000,
    LOAD_FP = 0b00001,
    _custom_0 = 0b00010,
    MISC_MEM = 0b00011,
    OP_IMM = 0b00100,
    AUIPC = 0b00101,
    OP_IMM_32 = 0b00110,

    STORE = 0b01000,
    STORE_FP = 0b01001,
    _custom_1 = 0b01010,
    AMO = 0b01011,
    OP = 0b01100,
    LUI = 0b01101,
    OP_32 = 0b01110,

    MADD = 0b10000,
    MSUB = 0b10001,
    NMSUB = 0b10010,
    NMADD = 0b10011,
    OP_FP = 0b10100,
    _reversed_0 = 0b10101,
    _custom_2_or_rv128 = 0b10110,

    BRANCH = 0b11000,
    JALR = 0b11001,
    _reversed_1 = 0b11010,
    JAL = 0b11011,
    SYSTEM = 0b11100,
    _reversed_2 = 0b11101,
    _custom_3_or_rv128 = 0b11110,
}
impl Into<i32> for OpCode {
    fn into(self) -> i32 {
        self as i32
    }
}
#[derive(Debug, Clone)]
pub struct InstructionName {
    pub pos: usize,
    pub name: String,
}
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
#[allow(non_camel_case_types)]
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
#[allow(non_camel_case_types)]
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
#[allow(non_camel_case_types)]
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
#[allow(non_camel_case_types)]
pub enum RV32E {
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
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
#[allow(non_camel_case_types)]
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
/// https://github.com/nervosnetwork/ckb-vm/issues/222
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RVV {
    VSETIVLI(Rd, Rs1, Imm32<20, 10>),
    VSETVLI(Rd, Rs1, Imm32<20, 11>),
    VSETVL(Rd, Rs1, Rs2),
    VLM_V(Rd, Rs1), // The first is v register and the second is normal register
    VLE8_V(Rd, Rs1, VM),
    VLE16_V(Rd, Rs1, VM),
    VLE32_V(Rd, Rs1, VM),
    VLE64_V(Rd, Rs1, VM),
    VLE128_V(Rd, Rs1, VM),
    VLE256_V(Rd, Rs1, VM),
    VLE512_V(Rd, Rs1, VM),
    VLE1024_V(Rd, Rs1, VM),
    VSM_V(Rs3, Rs1),
    VSE8_V(Rs3, Rs1, VM),
    VSE16_V(Rs3, Rs1, VM),
    VSE32_V(Rs3, Rs1, VM),
    VSE64_V(Rd, Rs1),
    VSE128_V(Rd, Rs1),
    VSE256_V(Rd, Rs1),
    VSE512_V(Rd, Rs1),
    VSE1024_V(Rd, Rs1),
    VADD_VV(Rd, Rs1),
    VADD_VX(Rd, Rs1),
    VADD_VI(Rd, Rs1),
    VSUB_VV(Rd, Rs1),
    VSUB_VX(Rd, Rs1),
    VRSUB_VX(Rd, Rs1),
    VRSUB_VI(Rd, Rs1),
    VMUL_VV(Rd, Rs1),
    VMUL_VX(Rd, Rs1),
    VDIV_VV(Rd, Rs1),
    VDIV_VX(Rd, Rs1),
    VDIVU_VV(Rd, Rs1),
    VDIVU_VX(Rd, Rs1),
    VREM_VV(Rd, Rs1),
    VREM_VX(Rd, Rs1),
    VREMU_VV(Rd, Rs1),
    VREMU_VX(Rd, Rs1),
    VSLL_VV(Rd, Rs1),
    VSLL_VX(Rd, Rs1),
    VSLL_VI(Rd, Rs1),
    VSRL_VV(Rd, Rs1),
    VSRL_VX(Rd, Rs1),
    VSRL_VI(Rd, Rs1),
    VSRA_VV(Rd, Rs1),
    VSRA_VX(Rd, Rs1),
    VSRA_VI(Rd, Rs1),
    VMSEQ_VV(Rd, Rs1),
    VMSEQ_VX(Rd, Rs1),
    VMSEQ_VI(Rd, Rs1),
    VMSNE_VV(Rd, Rs1),
    VMSNE_VX(Rd, Rs1),
    VMSNE_VI(Rd, Rs1),
    VMSLTU_VV(Rd, Rs1),
    VMSLTU_VX(Rd, Rs1),
    VMSLT_VV(Rd, Rs1),
    VMSLT_VX(Rd, Rs1),
    VMSLEU_VV(Rd, Rs1),
    VMSLEU_VX(Rd, Rs1),
    VMSLEU_VI(Rd, Rs1),
    VMSLE_VV(Rd, Rs1),
    VMSLE_VX(Rd, Rs1),
    VMSLE_VI(Rd, Rs1),
    VMSGTU_VX(Rd, Rs1),
    VMSGTU_VI(Rd, Rs1),
    VMSGT_VX(Rd, Rs1),
    VMSGT_VI(Rd, Rs1),
    VMINU_VV(Rd, Rs1),
    VMINU_VX(Rd, Rs1),
    VMIN_VV(Rd, Rs1),
    VMIN_VX(Rd, Rs1),
    VMAXU_VV(Rd, Rs1),
    VMAXU_VX(Rd, Rs1),
    VMAX_VV(Rd, Rs1),
    VMAX_VX(Rd, Rs1),
    VWADDU_VV(Rd, Rs1),
    VWADDU_VX(Rd, Rs1),
    VWSUBU_VV(Rd, Rs1),
    VWSUBU_VX(Rd, Rs1),
    VWADD_VV(Rd, Rs1),
    VWADD_VX(Rd, Rs1),
    VWSUB_VV(Rd, Rs1),
    VWSUB_VX(Rd, Rs1),
    VWADDU_WV(Rd, Rs1),
    VWADDU_WX(Rd, Rs1),
    VWSUBU_WV(Rd, Rs1),
    VWSUBU_WX(Rd, Rs1),
    VWADD_WV(Rd, Rs1),
    VWADD_WX(Rd, Rs1),
    VWSUB_WV(Rd, Rs1),
    VWSUB_WX(Rd, Rs1),
    VZEXT_VF8(Rd, Rs1),
    VSEXT_VF8(Rd, Rs1),
    VZEXT_VF4(Rd, Rs1),
    VSEXT_VF4(Rd, Rs1),
    VZEXT_VF2(Rd, Rs1),
    VSEXT_VF2(Rd, Rs1),
    VADC_VVM(Rd, Rs1),
    VADC_VXM(Rd, Rs1),
    VADC_VIM(Rd, Rs1),
    VMADC_VVM(Rd, Rs1),
    VMADC_VXM(Rd, Rs1),
    VMADC_VIM(Rd, Rs1),
    VMADC_VV(Rd, Rs1),
    VMADC_VX(Rd, Rs1),
    VMADC_VI(Rd, Rs1),
    VSBC_VVM(Rd, Rs1),
    VSBC_VXM(Rd, Rs1),
    VMSBC_VVM(Rd, Rs1),
    VMSBC_VXM(Rd, Rs1),
    VMSBC_VV(Rd, Rs1),
    VMSBC_VX(Rd, Rs1),
    VAND_VV(Rd, Rs1),
    VAND_VI(Rd, Rs1),
    VAND_VX(Rd, Rs1),
    VOR_VV(Rd, Rs1),
    VOR_VX(Rd, Rs1),
    VOR_VI(Rd, Rs1),
    VXOR_VV(Rd, Rs1),
    VXOR_VX(Rd, Rs1),
    VXOR_VI(Rd, Rs1),
    VNSRL_WV(Rd, Rs1),
    VNSRL_WX(Rd, Rs1),
    VNSRL_WI(Rd, Rs1),
    VNSRA_WV(Rd, Rs1),
    VNSRA_WX(Rd, Rs1),
    VNSRA_WI(Rd, Rs1),
    VMULH_VV(Rd, Rs1),
    VMULH_VX(Rd, Rs1),
    VMULHU_VV(Rd, Rs1),
    VMULHU_VX(Rd, Rs1),
    VMULHSU_VV(Rd, Rs1),
    VMULHSU_VX(Rd, Rs1),
    VWMULU_VV(Rd, Rs1),
    VWMULU_VX(Rd, Rs1),
    VWMULSU_VV(Rd, Rs1),
    VWMULSU_VX(Rd, Rs1),
    VWMUL_VV(Rd, Rs1),
    VWMUL_VX(Rd, Rs1),
    VMV_V_V(Rd, Rs1),
    VMV_V_X(Rd, Rs1),
    VMV_V_I(Rd, Rs1),
    VSADDU_VV(Rd, Rs1),
    VSADDU_VX(Rd, Rs1),
    VSADDU_VI(Rd, Rs1),
    VSADD_VV(Rd, Rs1),
    VSADD_VX(Rd, Rs1),
    VSADD_VI(Rd, Rs1),
    VSSUBU_VV(Rd, Rs1),
    VSSUBU_VX(Rd, Rs1),
    VSSUB_VV(Rd, Rs1),
    VSSUB_VX(Rd, Rs1),
    VAADDU_VV(Rd, Rs1),
    VAADDU_VX(Rd, Rs1),
    VAADD_VV(Rd, Rs1),
    VAADD_VX(Rd, Rs1),
    VASUBU_VV(Rd, Rs1),
    VASUBU_VX(Rd, Rs1),
    VASUB_VV(Rd, Rs1),
    VASUB_VX(Rd, Rs1),
    VMV1R_V(Rd, Rs1),
    VMV2R_V(Rd, Rs1),
    VMV4R_V(Rd, Rs1),
    VMV8R_V(Rd, Rs1),
    VFIRST_M(Rd, Rs1),
    VMAND_MM(Rd, Rs1),
    VMNAND_MM(Rd, Rs1),
    VMANDNOT_MM(Rd, Rs1),
    VMXOR_MM(Rd, Rs1),
    VMOR_MM(Rd, Rs1),
    VMNOR_MM(Rd, Rs1),
    VMORNOT_MM(Rd, Rs1),
    VMXNOR_MM(Rd, Rs1),
    VLSE8_V(Rd, Rs1),
    VLSE16_V(Rd, Rs1),
    VLSE32_V(Rd, Rs1),
    VLSE64_V(Rd, Rs1),
    VLSE128_V(Rd, Rs1),
    VLSE256_V(Rd, Rs1),
    VLSE512_V(Rd, Rs1),
    VLSE1024_V(Rd, Rs1),
    VSSE8_V(Rd, Rs1),
    VSSE16_V(Rd, Rs1),
    VSSE32_V(Rd, Rs1),
    VSSE64_V(Rd, Rs1),
    VSSE128_V(Rd, Rs1),
    VSSE256_V(Rd, Rs1),
    VSSE512_V(Rd, Rs1),
    VSSE1024_V(Rd, Rs1),
    VLUXEI8_V(Rd, Rs1),
    VLUXEI16_V(Rd, Rs1),
    VLUXEI32_V(Rd, Rs1),
    VLUXEI64_V(Rd, Rs1),
    VLUXEI128_V(Rd, Rs1),
    VLUXEI256_V(Rd, Rs1),
    VLUXEI512_V(Rd, Rs1),
    VLUXEI1024_V(Rd, Rs1),
    VLOXEI8_V(Rd, Rs1),
    VLOXEI16_V(Rd, Rs1),
    VLOXEI32_V(Rd, Rs1),
    VLOXEI64_V(Rd, Rs1),
    VLOXEI128_V(Rd, Rs1),
    VLOXEI256_V(Rd, Rs1),
    VLOXEI512_V(Rd, Rs1),
    VLOXEI1024_V(Rd, Rs1),
    VSUXEI8_V(Rd, Rs1),
    VSUXEI16_V(Rd, Rs1),
    VSUXEI32_V(Rd, Rs1),
    VSUXEI64_V(Rd, Rs1),
    VSUXEI128_V(Rd, Rs1),
    VSUXEI256_V(Rd, Rs1),
    VSUXEI512_V(Rd, Rs1),
    VSUXEI1024_V(Rd, Rs1),
    VSOXEI8_V(Rd, Rs1),
    VSOXEI16_V(Rd, Rs1),
    VSOXEI32_V(Rd, Rs1),
    VSOXEI64_V(Rd, Rs1),
    VSOXEI128_V(Rd, Rs1),
    VSOXEI256_V(Rd, Rs1),
    VSOXEI512_V(Rd, Rs1),
    VSOXEI1024_V(Rd, Rs1),
    VL1RE8_V(Rd, Rs1),
    VL1RE16_V(Rd, Rs1),
    VL1RE32_V(Rd, Rs1),
    VL1RE64_V(Rd, Rs1),
    VL2RE8_V(Rd, Rs1),
    VL2RE16_V(Rd, Rs1),
    VL2RE32_V(Rd, Rs1),
    VL2RE64_V(Rd, Rs1),
    VL4RE8_V(Rd, Rs1),
    VL4RE16_V(Rd, Rs1),
    VL4RE32_V(Rd, Rs1),
    VL4RE64_V(Rd, Rs1),
    VL8RE8_V(Rd, Rs1),
    VL8RE16_V(Rd, Rs1),
    VL8RE32_V(Rd, Rs1),
    VL8RE64_V(Rd, Rs1),
    VS1R_V(Rd, Rs1),
    VS2R_V(Rd, Rs1),
    VS4R_V(Rd, Rs1),
    VS8R_V(Rd, Rs1),
    VMACC_VV(Rd, Rs1),
    VMACC_VX(Rd, Rs1),
    VNMSAC_VV(Rd, Rs1),
    VNMSAC_VX(Rd, Rs1),
    VMADD_VV(Rd, Rs1),
    VMADD_VX(Rd, Rs1),
    VNMSUB_VV(Rd, Rs1),
    VNMSUB_VX(Rd, Rs1),
    VSSRL_VV(Rd, Rs1),
    VSSRL_VX(Rd, Rs1),
    VSSRL_VI(Rd, Rs1),
    VSSRA_VV(Rd, Rs1),
    VSSRA_VX(Rd, Rs1),
    VSSRA_VI(Rd, Rs1),
    VSMUL_VV(Rd, Rs1),
    VSMUL_VX(Rd, Rs1),
    VWMACCU_VV(Rd, Rs1),
    VWMACCU_VX(Rd, Rs1),
    VWMACC_VV(Rd, Rs1),
    VWMACC_VX(Rd, Rs1),
    VWMACCSU_VV(Rd, Rs1),
    VWMACCSU_VX(Rd, Rs1),
    VWMACCUS_VX(Rd, Rs1),
    VMERGE_VVM(Rd, Rs1),
    VMERGE_VXM(Rd, Rs1),
    VMERGE_VIM(Rd, Rs1),
    VNCLIPU_WV(Rd, Rs1),
    VNCLIPU_WX(Rd, Rs1),
    VNCLIPU_WI(Rd, Rs1),
    VNCLIP_WV(Rd, Rs1),
    VNCLIP_WX(Rd, Rs1),
    VNCLIP_WI(Rd, Rs1),
    VREDSUM_VS(Rd, Rs1),
    VREDAND_VS(Rd, Rs1),
    VREDOR_VS(Rd, Rs1),
    VREDXOR_VS(Rd, Rs1),
    VREDMINU_VS(Rd, Rs1),
    VREDMIN_VS(Rd, Rs1),
    VREDMAXU_VS(Rd, Rs1),
    VREDMAX_VS(Rd, Rs1),
    VWREDSUMU_VS(Rd, Rs1),
    VWREDSUM_VS(Rd, Rs1),
    VCPOP_M(Rd, Rs1),
    VMSBF_M(Rd, Rs1),
    VMSOF_M(Rd, Rs1),
    VMSIF_M(Rd, Rs1),
    VIOTA_M(Rd, Rs1),
    VID_V(Rd, Rs1),
    VMV_X_S(Rd, Rs1),
    VMV_S_X(Rd, Rs1),
    VCOMPRESS_VM(Rd, Rs1),
    VSLIDE1UP_VX(Rd, Rs1),
    VSLIDEUP_VX(Rd, Rs1),
    VSLIDEUP_VI(Rd, Rs1),
    VSLIDE1DOWN_VX(Rd, Rs1),
    VSLIDEDOWN_VX(Rd, Rs1),
    VSLIDEDOWN_VI(Rd, Rs1),
    VRGATHER_VX(Rd, Rs1),
    VRGATHER_VV(Rd, Rs1),
    VRGATHEREI16_VV(Rd, Rs1),
    VRGATHER_VI(Rd, Rs1),
}
/// typed RV32 instructions
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RV32Instr {
    RV32I(RV32I),
    RV32M(RV32M),
    RV32A(RV32A),
    RV32F(RV32F),
    RV32E(RV32E),
    RV32D(RV32D),
    RV32V(RVV),
    RV32Zifencei(RVZifencei),
    RV32Zcsr(RVZcsr),
}
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RV64I {
    LWU(Rd, Rs1, Imm32<11, 0>),
    LD(Rd, Rs1, Imm32<11, 0>),
    SD(Rs1, Rs2, Imm32<11, 0>),
    SLLI(Rd, Rs1, Shamt),
    SRLI(Rd, Rs1, Shamt),
    SRAI(Rd, Rs1, Shamt),
    ADDIW(Rd, Rs1, Imm32<11, 0>),
    SLLIW(Rd, Rs1, Shamt),
    SRLIW(Rd, Rs1, Shamt),
    SRAIW(Rd, Rs1, Shamt),
    ADDW(Rd, Rs1, Rs2),
    SUBW(Rd, Rs1, Rs2),
    SLLW(Rd, Rs1, Rs2),
    SRLW(Rd, Rs1, Rs2),
    SRAW(Rd, Rs1, Rs2),
}
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RV64M {
    MULW(Rd, Rs1, Rs2),
    DIVW(Rd, Rs1, Rs2),
    DIVUW(Rd, Rs1, Rs2),
    REMW(Rd, Rs1, Rs2),
    REMUW(Rd, Rs1, Rs2),
}
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RV64A {
    LR_D(Rd, Rs1, AQ, RL),
    SC_D(Rd, Rs1, Rs2, AQ, RL),
    AMOSWAP_D(Rd, Rs1, Rs2, AQ, RL),
    AMOADD_D(Rd, Rs1, Rs2, AQ, RL),
    AMOXOR_D(Rd, Rs1, Rs2, AQ, RL),
    AMOAND_D(Rd, Rs1, Rs2, AQ, RL),
    AMOOR_D(Rd, Rs1, Rs2, AQ, RL),
    AMOMIN_D(Rd, Rs1, Rs2, AQ, RL),
    AMOMAX_D(Rd, Rs1, Rs2, AQ, RL),
    AMOMINU_D(Rd, Rs1, Rs2, AQ, RL),
    AMOMAXU_D(Rd, Rs1, Rs2, AQ, RL),
}
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RV64F {
    FCVT_L_S(Rd, Rs1, RoundingMode),
    FCVT_LU_S(Rd, Rs1, RoundingMode),
    FCVT_S_L(Rd, Rs1, RoundingMode),
    FCVT_S_LU(Rd, Rs1, RoundingMode),
}
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RV64E {
    LWU(Rd, Rs1, Imm32<11, 0>),
    LD(Rd, Rs1, Imm32<11, 0>),
    SD(Rs1, Rs2, Imm32<11, 0>),
    SLLI(Rd, Rs1, Shamt),
    SRLI(Rd, Rs1, Shamt),
    SRAI(Rd, Rs1, Shamt),
    ADDIW(Rd, Rs1, Imm32<11, 0>),
    SLLIW(Rd, Rs1, Shamt),
    SRLIW(Rd, Rs1, Shamt),
    SRAIW(Rd, Rs1, Shamt),
    ADDW(Rd, Rs1, Rs2),
    SUBW(Rd, Rs1, Rs2),
    SLLW(Rd, Rs1, Rs2),
    SRLW(Rd, Rs1, Rs2),
    SRAW(Rd, Rs1, Rs2),
}
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RV64D {
    FCVT_L_D(Rd, Rs1, RoundingMode),
    FCVT_LU_D(Rd, Rs1, RoundingMode),
    FMV_X_D(Rd, Rs1),
    FCVT_D_L(Rd, Rs1, RoundingMode),
    FCVT_D_LU(Rd, Rs1, RoundingMode),
    FMV_D_X(Rd, Rs1),
}

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RVZcsr {
    CSRRW(Rd, Rs1, CSRAddr),
    CSRRS(Rd, Rs1, CSRAddr),
    CSRRC(Rd, Rs1, CSRAddr),
    CSRRWI(Rd, UImm, CSRAddr),
    CSRRSI(Rd, UImm, CSRAddr),
    CSRRCI(Rd, UImm, CSRAddr),
}
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RVZifencei {
    FENCE_I(Rd, Rs1, Imm32<11, 0>),
}

/// typed RV64 instructions
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RV64Instr {
    RV64I(RV64I),
    RV64M(RV64M),
    RV64A(RV64A),
    RV64F(RV64F),
    RV64E(RV64E),
    RV64D(RV64D),
    RV64V(RVV),
    RV64Zifencei(RVZifencei),
    RV64Zcsr(RVZcsr),
}

/// typed RV64 instructions
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RV128Instr {
    RV128V(RVV),
    RV128Zifencei(RVZifencei),
    RV128Zcsr(RVZcsr),
}

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
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Instr {
    RV32(RV32Instr),
    RV64(RV64Instr),
    RV128(RV128Instr),
    NOP,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct CSRAddr(pub Imm32<11, 0>);

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct UImm(pub Imm32<4, 0>);

impl CSRAddr {
    pub fn value(self) -> u16 {
        self.0.decode() as u16
    }
}

impl UImm {
    pub fn value(self) -> u32 {
        self.0.decode()
    }
}

#[derive(Debug, Clone)]
pub struct Instruction {
    pub name: InstructionName,
    pub field: InstructionField,
    pub Instr: Instr,
}
#[inline(always)]
pub fn x(instruction_bits: u32, lower: usize, length: usize, shifts: usize) -> u32 {
    ((instruction_bits >> lower) & ((1 << length) - 1)) << shifts
}
impl Instruction {
    fn parse(bit: &[u8]) -> Instruction {
        Instruction {
            name: InstructionName {
                pos: 0,
                name: "".to_string(),
            },
            field: todo!(),
            Instr: todo!(),
        }
    }
    fn opcode(self) -> OpCode {
        todo!()
    }
    fn rd(self) -> Reg {
        todo!()
    }
    fn rs1(self) -> Reg {
        todo!()
    }
    fn imm<const HIGH_BIT: usize, const LOW_BIT: usize>(self) -> Imm32<HIGH_BIT, LOW_BIT> {
        todo!()
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
        let mut bytes = [0; 32];
        //    self.address, &mut bytes
        let mut address = self.address;
        let mut read_len = 0;
        let mut data: &mut [u8] = &mut bytes;
        let PageIndexOfs {
            page_index,
            mut offset,
        } = Page::valid_page_index_and_offset(address).unwrap();
        for page_index in page_index.. {
            if let Some(page) = self
                .memory_map
                .get(page_index)
                .and_then(Option::as_ref)
                .map(|v| &(*v))
            {
                let the_rest_page = &page[offset..];
                if data.len() <= the_rest_page.len() {
                    data.copy_from_slice(&the_rest_page[..data.len()]);
                    read_len += data.len();
                }
                let (current_data, data_rest) = data.split_at_mut(the_rest_page.len());
                current_data.copy_from_slice(the_rest_page);
                read_len += current_data.len();
                data = data_rest;
                address += the_rest_page.len() as u64;
                offset = 0;
            } else {
                continue;
            }
        }

        let instruction = Instruction::parse(&bytes[..read_len]);
        Some((address, instruction))
    }
}

#[cfg(test)]
mod tests {
    use crate::frontend::instruction::Instruction;

    #[test]
    fn test_layout() {
        // jal x0, -6*4
        let instr_asm: u32 = 0b_1_1111110100_1_11111111_00000_1101111;
        let instr = Instruction::parse(&instr_asm.to_le_bytes());
        // assert_eq!(instr.opcode(), OpCode(0b_1101111));
        // assert_eq!(instr.rd(), Reg(0b0));
        // assert_eq!(instr.imm(), 0b11111111);
        // assert_eq!(instr.imm11(), 0b1);
        // assert_eq!(instr.imm10_1(), 0b1111110100);
        // assert_eq!(instr.imm20(), 0b1);
    }
}
