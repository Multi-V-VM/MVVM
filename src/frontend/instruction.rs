use crate::frontend::{
    // instructions::{rv128, rv32, rv64},
    BIT_LENGTH,
    IS_E,
};
use crate::{rv128, rv32, rv64};

use super::page::{Page, PageIndexOfs};
use core::panic;
use std::fmt;
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

/// Xx From X0-X15 or X31 or X63
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Xx<const MAX: u32>(u32);

impl<const MAX: u32> Xx<MAX> {
    pub fn new(value: u32) -> Self {
        Self(value)
    }
}
/// Reg
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Reg {
    X(Xx<32>),
    F(Xx<32>),
    V(Xx<32>),
    PC,
    FCSR,
}
impl fmt::Debug for Reg {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            Reg::X(ref x) => write!(f, "x{}", x.0),
            Reg::F(ref x) => write!(f, "f{}", x.0),
            Reg::V(ref x) => write!(f, "v{}", x.0),
            Reg::PC => write!(f, "pc"),
            Reg::FCSR => write!(f, "fcsr"),
        }
    }
}

/// Floating-point rounding mode
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum RoundingMode {
    /// Round to nearest, ties to even
    RNE = 0b000,
    /// Round towards zero
    RTZ = 0b001,
    /// Round towards -inXxity
    RDN = 0b010,
    /// Round towards +inXxity
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

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RV32I {
    LUI(Rd, Imm32<31, 12>),
    AUIPC(Rd, Imm32<31, 12>),
    JAL(Rd, Imm32<20, 1>),
    JALR(Rd, Rs1, Imm32<11, 0>),
    BEQ(Rs1, Rs2, Imm32<12, 1>),
    BNE(Rs1, Rs2, Imm32<12, 1>),
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
    JAL(Rd, Imm32<20, 1>),
    JALR(Rd, Rs1, Imm32<11, 0>),
    BEQ(Rs1, Rs2, Imm32<12, 1>),
    BNE(Rs1, Rs2, Imm32<12, 1>),
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
    RVB(RVB),
    RVV(RVV),
    RVZifencei(RVZifencei),
    RVZcsr(RVZcsr),
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
pub enum RV128I {
    LDU(Rd, Rs1, Imm32<11, 0>),
    LD(Rd, Rs1, Imm32<11, 0>),
    SD(Rs1, Rs2, Imm32<11, 0>),
    SLLI(Rd, Rs1, Shamt),
    SRLI(Rd, Rs1, Shamt),
    SRAI(Rd, Rs1, Shamt),
    ADDID(Rd, Rs1, Imm32<11, 0>),
    SLLID(Rd, Rs1, Shamt),
    SRLID(Rd, Rs1, Shamt),
    SRAID(Rd, Rs1, Shamt),
    ADDD(Rd, Rs1, Rs2),
    SUBD(Rd, Rs1, Rs2),
    SLLD(Rd, Rs1, Rs2),
    SRLD(Rd, Rs1, Rs2),
    SRAD(Rd, Rs1, Rs2),
}
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RVPreviledge {
    SRET,
    MRET,
    WFI,
    SFENCE_VMA(Rs1, Rs2),
    SINVAL_VMA(Rs1, Rs2),
    SFENCE_W_INVAL,
    SFENCE_INVAL_IR,
}
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RVB {
    ADDUW(Rd, Rs1, Rs2),
    ANDN(Rd, Rs1, Rs2),
    BCLR(Rd, Rs1, Rs2),
    BCLRI(Rd, Rs1, Imm32<11, 0>),
    BEXT(Rd, Rs1, Rs2),
    BEXTI(Rd, Rs1, Imm32<11, 0>),
    BINV(Rd, Rs1, Rs2),
    BINVI(Rd, Rs1, Imm32<11, 0>),
    BSET(Rd, Rs1, Rs2),
    BSETI(Rd, Rs1, Imm32<11, 0>),
    CLMUL(Rd, Rs1, Rs2),
    CLMULH(Rd, Rs1, Rs2),
    CLMULR(Rd, Rs1, Rs2),
    CLZ(Rd, Rs),
    CLZW(Rd, Rs),
    CPOP(Rd, Rs),
    CPOPW(Rd, Rs),
    CTZ(Rd, Rs),
    CTZW(Rd, Rs),
    MAX(Rd, Rs1, Rs2),
    MAXU(Rd, Rs1, Rs2),
    MIN(Rd, Rs1, Rs2),
    MINU(Rd, Rs1, Rs2),
    ORCB(Rd, Rs1, Rs2),
    ORN(Rd, Rs1, Rs2),
    REV8(Rd, Rs),
    ROL(Rd, Rs1, Rs2),
    ROLW(Rd, Rs1, Rs2),
    ROR(Rd, Rs1, Rs2),
    RORI(Rd, Rs1, Shamt),
    RORIW(Rd, Rs1, Shamt),
    RORW(Rd, Rs1, Rs2),
    SEXTB(Rd, Rs),
    SEXTH(Rd, Rs),
    SH1ADD(Rd, Rs1, Rs2),
    SH1ADDUW(Rd, Rs1, Rs2),
    SH2ADD(Rd, Rs1, Rs2),
    SH2ADDUW(Rd, Rs1, Rs2),
    SH3ADD(Rd, Rs1, Rs2),
    SH3ADDUW(Rd, Rs1, Rs2),
    SLLIUW(Rd, Rs1, Rs2),
    XNOR(Rd, Rs1, Rs2),
    ZEXTH(Rd, Rs),
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
    RVB(RVB),
    RV64V(RVV),
    RVZifencei(RVZifencei),
    RVZcsr(RVZcsr),
    RVPreviledge(RVPreviledge),
}

/// typed RV64 instructions
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
#[allow(non_camel_case_types)]
pub enum RV128Instr {
    RV128I(RV128I),
    RVV(RVV),
    RVZifencei(RVZifencei),
    RVZcsr(RVZcsr),
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct FenceFm(pub Xx<16>);
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct FencePred(pub Xx<16>);
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct FenceSucc(pub Xx<16>);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Instr {
    RV32(RV32Instr),
    RV64(RV64Instr),
    RV128(RV128Instr),
    NOP,
}

impl fmt::Display for RV32Instr {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Ok(())
    }
} // TODO: implement the rv32
impl fmt::Display for RV64Instr {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Ok(())
    }
} // TODO: implement the rv64
impl fmt::Display for RV128Instr {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Ok(())
    }
} // TODO: implement the rv128

impl Instr {
    pub fn get_arch(self) -> u32 {
        match self {
            Instr::NOP => 0,
            Instr::RV32(_) => 32,
            Instr::RV64(_) => 64,
            Instr::RV128(_) => 128,
        }
    }
    pub fn get_type(self) -> String {
        match self {
            Instr::NOP => "NOP".to_owned(),
            Instr::RV32(inst) => inst.to_string(),
            Instr::RV64(inst) => inst.to_string(),
            Instr::RV128(inst) => inst.to_string(),
        }
    }
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
    pub instr: Instr,
}

#[inline(always)]
pub fn slice(bit: u32, lo: usize, len: usize, s: usize) -> u32 {
    ((bit >> lo) & ((1 << len) - 1)) << s
}
#[inline(always)]
pub fn slice_back(bit: u32, lo: usize, len: usize, s: usize) -> u32 {
    ((bit as i32) << (32 - lo - len) >> (32 - len) << s) as u32
}
#[inline(always)]
pub fn funct2(bit: u32) -> u32 {
    slice(bit, 25, 2, 0)
}
#[inline(always)]
pub fn funct3(bit: u32) -> u32 {
    slice(bit, 12, 3, 0)
}
#[inline(always)]
pub fn funct5(bit: u32) -> u32 {
    slice(bit, 27, 5, 0)
}
#[inline(always)]
pub fn funct7(bit: u32) -> u32 {
    slice(bit, 25, 7, 0)
}
#[inline(always)]
pub fn aq(bit: u32) -> bool {
    slice(bit, 26, 1, 0) != 0
}
#[inline(always)]
pub fn rl(bit: u32) -> bool {
    slice(bit, 25, 1, 0) != 0
}
#[inline(always)]
pub fn rd(bit: u32) -> u32 {
    slice(bit, 7, 5, 0)
}

#[inline(always)]
pub fn rs1(bit: u32) -> u32 {
    slice(bit, 15, 5, 0)
}

#[inline(always)]
pub fn rs2(bit: u32) -> u32 {
    slice(bit, 20, 5, 0) as u32
}
#[inline(always)]
pub fn rs3(bit: u32) -> u32 {
    slice(bit, 27, 5, 0)
}
#[inline(always)]
pub fn shamt(bit: u32) -> usize {
    slice(bit, 20, 5, 0) as usize
}

#[inline(always)]
pub fn btype_immediate(bit: u32) -> u32 {
    (slice(bit, 8, 4, 1) | slice(bit, 25, 6, 5) | slice(bit, 7, 1, 11) | slice_back(bit, 31, 1, 12))
        as u32
}

#[inline(always)]
pub fn jtype_immediate(bit: u32) -> u32 {
    (slice(bit, 21, 10, 1)
        | slice(bit, 20, 1, 11)
        | slice(bit, 12, 8, 12)
        | slice_back(bit, 31, 1, 20)) as u32
}

#[inline(always)]
pub fn itype_immediate(bit: u32) -> u32 {
    slice_back(bit, 20, 12, 0) as u32
}

#[inline(always)]
pub fn stype_immediate(bit: u32) -> u32 {
    (slice(bit, 7, 5, 0) | slice_back(bit, 25, 7, 5)) as u32
}

#[inline(always)]
pub fn utype_immediate(bit: u32) -> u32 {
    slice_back(bit, 12, 20, 12) as u32
}
// Notice the location of rs2 in RVC encoding is different from full encoding
#[inline(always)]
fn c_rs2(bit_u32: u32) -> u32 {
    slice(bit_u32, 2, 5, 0)
}

// This function extract 3 bits from least_bit to form a register number,
// here since we are only using 3 bits, we can only reference the most popular
// used registers x8 - x15. In other words, a number of 0 extracted here means
// x8, 1 means x9, etc.
#[inline(always)]
fn c_r(bit_u32: u32, least_bit: usize) -> u32 {
    slice(bit_u32, least_bit, 3, 0) + 8
}

// [12]  => imm[5]
// [6:2] => imm[4:0]
fn c_immediate(bit_u32: u32) -> u32 {
    slice(bit_u32, 2, 5, 0) | slice_back(bit_u32, 12, 1, 5)
}

// [12]  => imm[5]
// [6:2] => imm[4:0]
fn c_uimmediate(bit_u32: u32) -> u32 {
    slice(bit_u32, 2, 5, 0) | slice(bit_u32, 12, 1, 5)
}

// [12:2] => imm[11|4|9:8|10|6|7|3:1|5]
fn c_j_immediate(bit_u32: u32) -> u32 {
    slice(bit_u32, 3, 3, 1)
        | slice(bit_u32, 11, 1, 4)
        | slice(bit_u32, 2, 1, 5)
        | slice(bit_u32, 7, 1, 6)
        | slice(bit_u32, 6, 1, 7)
        | slice(bit_u32, 9, 2, 8)
        | slice(bit_u32, 8, 1, 10)
        | slice_back(bit_u32, 12, 1, 11)
}

// [12:10] => uimm[5:3]
// [6:5]   => uimm[7:6]
fn c_fld_uimmediate(bit_u32: u32) -> u32 {
    slice(bit_u32, 10, 3, 3) | slice(bit_u32, 5, 2, 6)
}

// [10:12] => uimm[5:3]
// [5:6]   => uimm[2|6]
fn c_sw_uimmediate(bit_u32: u32) -> u32 {
    slice(bit_u32, 6, 1, 2) | slice(bit_u32, 10, 3, 3) | slice(bit_u32, 5, 1, 6)
}

// [12]  => uimm[5]
// [6:2] => uimm[4:2|7:6]
fn c_lwsp_uimmediate(bit_u32: u32) -> u32 {
    slice(bit_u32, 4, 3, 2) | slice(bit_u32, 12, 1, 5) | slice(bit_u32, 2, 2, 6)
}

// [12]  => uimm[5]
// [6:2] => uimm[4:3|8:6]
fn c_fldsp_uimmediate(bit_u32: u32) -> u32 {
    slice(bit_u32, 5, 2, 3) | slice(bit_u32, 12, 1, 5) | slice(bit_u32, 2, 3, 6)
}

// [12:7] => uimm[5:3|8:6]
fn c_fsdsp_uimmediate(bit_u32: u32) -> u32 {
    slice(bit_u32, 10, 3, 3) | slice(bit_u32, 7, 3, 6)
}

// [12:7] => uimm[5:2|7:6]
fn c_swsp_uimmediate(bit_u32: u32) -> u32 {
    slice(bit_u32, 9, 4, 2) | slice(bit_u32, 7, 2, 6)
}

// [12:10] => imm[8|4:3]
// [6:2]   => imm[7:6|2:1|5]
fn c_b_immediate(bit_u32: u32) -> u32 {
    slice(bit_u32, 3, 2, 1)
        | slice(bit_u32, 10, 2, 3)
        | slice(bit_u32, 2, 1, 5)
        | slice(bit_u32, 5, 2, 6)
        | slice_back(bit_u32, 12, 1, 8)
}
#[inline(always)]
fn try_from_compressed(bit: &[u8]) -> Option<Instruction> {
    let bit_u32 = u32::from_le_bytes(bit.split_at(4).0.try_into().unwrap());
    match bit_u32 & 0b111_00000000000_11 {
        // == Quadrant 0
        0b000_00000000000_00 => {
            let nzuimm = slice(bit_u32, 6, 1, 2)
                | slice(bit_u32, 5, 1, 3)
                | slice(bit_u32, 11, 2, 4)
                | slice(bit_u32, 7, 4, 6);
            if nzuimm != 0 {
                // C.ADDI4SPN
                Some(rv32!(
                    RV32I,
                    ADDI,
                    Rd(Reg::X(Xx::new(c_r(bit_u32, 2)))),
                    Rs1(Reg::X(Xx::new(2))),
                    Imm32::from(nzuimm as u32)
                ))
            } else {
                // Illegal instruction
                None
            }
        }
        0b010_00000000000_00 => Some(rv32!(
            RV32I,
            LW,
            Rd(Reg::X(Xx::new(c_r(bit_u32, 2)))),
            Rs1(Reg::X(Xx::new(c_r(bit_u32, 7)))),
            Imm32::from(c_sw_uimmediate(bit_u32))
        )),
        0b011_00000000000_00 => {
            // C.FLD
            if unsafe { BIT_LENGTH == 1 } {
                Some(rv64!(
                    RV64I,
                    LD,
                    Rd(Reg::X(Xx::new(c_r(bit_u32, 2)))),
                    Rs1(Reg::X(Xx::new(c_r(bit_u32, 7)))),
                    Imm32::from(c_fld_uimmediate(bit_u32))
                ))
            } else if unsafe { BIT_LENGTH == 2 } {
                Some(rv128!(
                    RV128I,
                    LD,
                    Rd(Reg::X(Xx::new(c_r(bit_u32, 2)))),
                    Rs1(Reg::X(Xx::new(c_r(bit_u32, 7)))),
                    Imm32::from(c_fld_uimmediate(bit_u32))
                ))
            } else {
                None
            }
        }
        // Reserved
        0b100_00000000000_00 => None,
        0b110_00000000000_00 => Some(
            // C.SW
            rv32!(
                RV32I,
                SW,
                Rs1(Reg::X(Xx::new(c_r(bit_u32, 7)))),
                Rs2(Reg::X(Xx::new(c_r(bit_u32, 2)))),
                Imm32::from(c_sw_uimmediate(bit_u32))
            ),
        ),
        0b111_00000000000_00 => {
            // C.SD
            if unsafe { BIT_LENGTH == 1 } {
                Some(rv64!(
                    RV64I,
                    SD,
                    Rs1(Reg::X(Xx::new(c_r(bit_u32, 7)))),
                    Rs2(Reg::X(Xx::new(c_r(bit_u32, 2)))),
                    Imm32::from(c_fld_uimmediate(bit_u32))
                ))
            } else if unsafe { BIT_LENGTH == 2 } {
                Some(rv128!(
                    RV128I,
                    SD,
                    Rs1(Reg::X(Xx::new(c_r(bit_u32, 7)))),
                    Rs2(Reg::X(Xx::new(c_r(bit_u32, 2)))),
                    Imm32::from(c_fld_uimmediate(bit_u32))
                ))
            } else {
                None
            }
        }
        // == Quadrant 1
        0b000_00000000000_01 => {
            let nzimm = c_immediate(bit_u32);
            let rd = rd(bit_u32);
            if rd != 0 {
                // C.ADDI
                if nzimm != 0 {
                    Some(rv32!(
                        RV32I,
                        ADDI,
                        Rd(Reg::X(Xx::new(rd))),
                        Rs1(Reg::X(Xx::new(rd))),
                        Imm32::from(nzimm)
                    ))
                } else {
                    // HINTs
                    Some(Instr::NOP)
                }
            } else {
                // C.NOP
                if nzimm == 0 {
                    Some(Instr::NOP)
                } else {
                    // HINTs
                    Some(Instr::NOP)
                }
            }
        }
        0b001_00000000000_01 => {
            if unsafe { BIT_LENGTH == 0 } {
                // C.JAL
                Some(rv32!(
                    RV32I,
                    JAL,
                    Rd(Reg::X(Xx::new(1))),
                    Imm32::from(c_j_immediate(bit_u32))
                ))
            } else {
                let rd = rd(bit_u32);
                if rd != 0 {
                    // C.ADDIW
                    Some(rv64!(
                        RV64I,
                        ADDIW,
                        Rd(Reg::X(Xx::new(rd))),
                        Rs1(Reg::X(Xx::new(rd))),
                        Imm32::from(c_immediate(bit_u32))
                    ))
                } else {
                    None
                }
            }
        }
        0b010_00000000000_01 => {
            let rd = rd(bit_u32);
            if rd != 0 {
                // C.LI
                Some(rv32!(
                    RV32I,
                    ADDI,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(0))),
                    Imm32::from(c_immediate(bit_u32))
                ))
            } else {
                // HINTs
                Some(Instr::NOP)
            }
        }
        0b011_00000000000_01 => {
            let imm = c_immediate(bit_u32) << 12;
            if imm != 0 {
                let rd = rd(bit_u32);
                if rd == 2 {
                    // C.ADDI16SP
                    Some(rv32!(
                        RV32I,
                        ADDI,
                        Rd(Reg::X(Xx::new(2))),
                        Rs1(Reg::X(Xx::new(2))),
                        Imm32::from(
                            slice(bit_u32, 6, 1, 4)
                                | slice(bit_u32, 2, 1, 5)
                                | slice(bit_u32, 5, 1, 6)
                                | slice(bit_u32, 3, 2, 7)
                                | slice_back(bit_u32, 12, 1, 9)
                        )
                    ))
                } else {
                    // C.LUI
                    if rd != 0 {
                        Some(rv32!(RV32I, LUI, Rd(Reg::X(Xx::new(rd))), Imm32::from(imm)))
                    } else {
                        // HINTs
                        Some(Instr::NOP)
                    }
                }
            } else {
                None
            }
        }
        0b100_00000000000_01 => {
            let rd = c_r(bit_u32, 7);
            match bit_u32 & 0b1_11_000_11000_00 {
                // C.SRLI64
                0b0_00_000_00000_00 if bit_u32 & 0b111_00 == 0 => Some(Instr::NOP),
                // C.SRAI64
                0b0_01_000_00000_00 if bit_u32 & 0b111_00 == 0 => Some(Instr::NOP),
                // C.SUB
                0b0_11_000_00000_00 => Some(rv32!(
                    RV32I,
                    SUB,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(rd))),
                    Rs2(Reg::X(Xx::new(c_r(bit_u32, 2))))
                )),
                // C.XOR
                0b0_11_000_01000_00 => Some(rv32!(
                    RV32I,
                    XOR,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(rd))),
                    Rs2(Reg::X(Xx::new(c_r(bit_u32, 2))))
                )),
                // C.OR
                0b0_11_000_10000_00 => Some(rv32!(
                    RV32I,
                    OR,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(rd))),
                    Rs2(Reg::X(Xx::new(c_r(bit_u32, 2))))
                )),
                // C.AND
                0b0_11_000_11000_00 => Some(rv32!(
                    RV32I,
                    AND,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(rd))),
                    Rs2(Reg::X(Xx::new(c_r(bit_u32, 2))))
                )),
                // C.SUBW
                0b1_11_000_00000_00 => Some(rv64!(
                    RV64I,
                    SUBW,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(rd))),
                    Rs2(Reg::X(Xx::new(c_r(bit_u32, 2))))
                )),
                // C.ADDW
                0b1_11_000_01000_00 => Some(rv64!(
                    RV64I,
                    ADDW,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(rd))),
                    Rs2(Reg::X(Xx::new(c_r(bit_u32, 2))))
                )),
                // Reserved
                0b1_11_000_10000_00 => None,
                // Reserved
                0b1_11_000_11000_00 => None,
                _ => {
                    let uimm = c_uimmediate(bit_u32);
                    match (bit_u32 & 0b11_000_00000_00, uimm) {
                        // Invalid instruction
                        (0b00_000_00000_00, 0) => None,
                        // C.SRLI
                        (0b00_000_00000_00, uimm) => Some(rv64!(
                            RV64I,
                            SRLI,
                            Rd(Reg::X(Xx::new(rd))),
                            Rs1(Reg::X(Xx::new(rd))),
                            Shamt((((bit_u32 >> 7) & 0x20) | ((bit_u32 >> 2) & 0x1f)) as u8)
                        )),
                        // Invalid instruction
                        (0b01_000_00000_00, 0) => None,
                        // C.SRAI
                        (0b01_000_00000_00, uimm) => Some(rv64!(
                            RV64I,
                            SRAI,
                            Rd(Reg::X(Xx::new(rd))),
                            Rs1(Reg::X(Xx::new(rd))),
                            Shamt(((bit_u32 >> 7) & 0x20) as u8 | ((bit_u32 >> 2) & 0x1f) as u8)
                        )),
                        // C.ANDI
                        (0b10_000_00000_00, _) => Some(rv32!(
                            RV32I,
                            ANDI,
                            Rd(Reg::X(Xx::new(rd))),
                            Rs1(Reg::X(Xx::new(rd))),
                            Imm32::from(c_immediate(bit_u32))
                        )),
                        _ => None,
                    }
                }
            }
        }
        // C.J
        0b101_00000000000_01 => Some(rv32!(
            RV32I,
            JAL,
            Rd(Reg::X(Xx::new(0))),
            Imm32::from(c_j_immediate(bit_u32))
        )),
        // C.BEQZ
        0b110_00000000000_01 => Some(rv32!(
            RV32I,
            BEQ,
            Rs1(Reg::X(Xx::new(c_r(bit_u32, 7)))),
            Rs2(Reg::X(Xx::new(0))),
            Imm32::from(c_b_immediate(bit_u32))
        )),
        // C.BNEZ
        0b111_00000000000_01 => Some(rv32!(
            RV32I,
            BEQ,
            Rs1(Reg::X(Xx::new(c_r(bit_u32, 7)))),
            Rs2(Reg::X(Xx::new(0))),
            Imm32::from(c_b_immediate(bit_u32))
        )),
        // == Quadrant 2
        0b000_00000000000_10 => {
            let uimm = c_uimmediate(bit_u32);
            let rd = rd(bit_u32);
            if rd != 0 && uimm != 0 {
                // C.SLLI
                Some(rv64!(
                    RV64I,
                    SLLI,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(rd))),
                    Shamt(uimm as u8)
                ))
            } else {
                // HINTs
                Some(Instr::NOP)
            }
        }
        0b010_00000000000_10 => {
            let rd = rd(bit_u32);
            if rd != 0 {
                // C.LWSP
                Some(rv32!(
                    RV32I,
                    LW,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(2))),
                    Imm32::from(c_lwsp_uimmediate(bit_u32))
                ))
            } else {
                // Reserved
                None
            }
        }
        0b011_00000000000_10 => {
            if unsafe { BIT_LENGTH == 0 } {
                None
            } else {
                let rd = rd(bit_u32);
                if rd != 0 {
                    // C.LDSP
                    Some(rv64!(
                        RV64I,
                        LD,
                        Rd(Reg::X(Xx::new(rd))),
                        Rs1(Reg::X(Xx::new(2))),
                        Imm32::from(c_fldsp_uimmediate(bit_u32))
                    ))
                } else {
                    // Reserved
                    None
                }
            }
        }
        0b100_00000000000_10 => {
            match bit_u32 & 0b1_00000_00000_00 {
                0b0_00000_00000_00 => {
                    let rd = rd(bit_u32);
                    let rs2 = c_rs2(bit_u32);
                    if rs2 == 0 {
                        if rd != 0 {
                            // C.JR
                            Some(rv32!(
                                RV32I,
                                JALR,
                                Rd(Reg::X(Xx::new(0))),
                                Rs1(Reg::X(Xx::new(rd))),
                                Imm32::from(0)
                            ))
                        } else {
                            // Reserved
                            None
                        }
                    } else {
                        if rd != 0 {
                            // C.MV
                            Some(rv32!(
                                RV32I,
                                ADD,
                                Rd(Reg::X(Xx::new(rd))),
                                Rs1(Reg::X(Xx::new(0))),
                                Rs2(Reg::X(Xx::new(rs2)))
                            ))
                        } else {
                            // HINTs
                            Some(Instr::NOP)
                        }
                    }
                }
                0b1_00000_00000_00 => {
                    let rd = rd(bit_u32);
                    let rs2 = c_rs2(bit_u32);
                    match (rd, rs2) {
                        // C.EBREAK
                        (0, 0) => Some(rv32!(RV32I, EBREAK)),
                        // C.JALR
                        (rs1, 0) => Some(rv32!(
                            RV32I,
                            JALR,
                            Rd(Reg::X(Xx::new(1))),
                            Rs1(Reg::X(Xx::new(rs1))),
                            Imm32::from(0)
                        )),
                        // C.ADD
                        (rd, rs2) => {
                            if rd != 0 {
                                Some(rv32!(
                                    RV32I,
                                    ADD,
                                    Rd(Reg::X(Xx::new(rd))),
                                    Rs1(Reg::X(Xx::new(rd))),
                                    Rs2(Reg::X(Xx::new(rs2)))
                                ))
                            } else {
                                // HINTs
                                Some(Instr::NOP)
                            }
                        }
                    }
                }
                _ => unreachable!(),
            }
        }
        0b110_00000000000_10 => Some(
            // C.SWSP
            rv32!(
                RV32I,
                SW,
                Rs1(Reg::X(Xx::new(2))),
                Rs2(Reg::X(Xx::new(c_rs2(bit_u32)))),
                Imm32::from(c_swsp_uimmediate(bit_u32))
            ),
        ),
        0b111_00000000000_10 => {
            if unsafe { BIT_LENGTH == 0 } {
                None
            } else {
                // C.SDSP
                Some(rv32!(
                    RV32I,
                    SW,
                    Rs1(Reg::X(Xx::new(2))),
                    Rs2(Reg::X(Xx::new(c_rs2(bit_u32)))),
                    Imm32::from(c_swsp_uimmediate(bit_u32))
                ))
            }
        }
        _ => None,
    }
    .map_or(None, |x| Some(Instruction { instr: x }))
}
pub fn fp(reg: u8) -> Reg {
    let encoding = reg as u8;
    if encoding <= 31 {
        Reg::F(Xx::new(encoding as u32))
    } else {
        panic!("Inaccessible register encoding: {:b}", encoding)
    }
}

pub fn gp(reg: u8) -> Reg {
    let encoding = reg as u8;
    if encoding <= 31 {
        Reg::X(Xx::new(encoding as u32))
    } else {
        panic!("Inaccessible register encoding: {:b}", encoding)
    }
}

#[macro_export]
macro_rules! rv32_no_e {
    ($ident1:ident,$ident2:ident) => { Instr::RV32(RV32Instr::$ident1($ident1::$ident2)) };
    ($ident1:ident,$ident2:ident, $($t:expr),*) => { Instr::RV32(RV32Instr::$ident1($ident1::$ident2($( $t, )*))) };
}
#[macro_export]
macro_rules! rv32 {
    ($ident1:ident,$ident2:ident) => {
        unsafe {
            if IS_E && String::from(stringify!($ident1)) == "RV32I" {
                Instr::RV32(RV32Instr::RV32E(RV32E::$ident2))
            } else {
                Instr::RV32(RV32Instr::$ident1($ident1::$ident2))
            }
        }
    };
    ($ident1:ident,$ident2:ident, $($t:expr),*) => {
        unsafe {
            if IS_E && String::from(stringify!($ident1)) == "RV32I" {
                Instr::RV32(RV32Instr::RV32E(RV32E::$ident2($( $t, )*)))
            } else {
                Instr::RV32(RV32Instr::$ident1($ident1::$ident2($( $t, )*)))
            }
        }
    };
}
#[macro_export]
macro_rules! rv64_no_e {
    ($ident1:ident,$ident2:ident) => { Instr::RV64(RV64Instr::$ident1($ident1::$ident2)) };
    ($ident1:ident,$ident2:ident, $($t:expr),*) => { Instr::RV64(RV64Instr::$ident1($ident1::$ident2($( $t, )*))) };
}
#[macro_export]
macro_rules! rv64 {
    ($ident1:ident,$ident2:ident) => {
        unsafe {
            if IS_E && String::from(stringify!($ident1)) == "RV64I" {
                Instr::RV64(RV64Instr::RV64E(RV64E::$ident2))
            } else {
                Instr::RV64(RV64Instr::$ident1($ident1::$ident2))
            }
        }
    };
    ($ident1:ident,$ident2:ident, $($t:expr),*) => {
        unsafe {
            if IS_E && String::from(stringify!($ident1)) == "RV64I" {
                Instr::RV64(RV64Instr::RV64E(RV64E::$ident2($( $t, )*)))
            } else {
                Instr::RV64(RV64Instr::$ident1($ident1::$ident2($( $t, )*)))
            }
        }
    };
}
#[macro_export]
macro_rules! rv128 {
    ($ident1:ident,$ident2:ident) => { Instr::RV128(RV128Instr::$ident1($ident1::$ident2)) };
    ($ident1:ident,$ident2:ident, $($t:expr),*) => { Instr::RV128(RV128Instr::$ident1($ident1::$ident2($( $t, )*))) };
}
#[macro_export]
macro_rules! i {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $reg:ident) => {{
        let rd = Rd($reg(rd($bit).try_into().unwrap()));
        let rs1 = Rs1($reg(rs1($bit).try_into().unwrap()));
        let imm = Imm32::<11, 0>::from(itype_immediate($bit) as u32);
        $type!($opcode1, $opcode2, rd, rs1, imm)
    }};
}
macro_rules! b {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $reg:ident) => {{
        let rs1 = Rs1($reg(rs1($bit).try_into().unwrap()));
        let rs2 = Rs2($reg(rs2($bit).try_into().unwrap()));
        let imm = Imm32::<12, 1>::from(btype_immediate($bit) as u32);
        $type!($opcode1, $opcode2, rs1, rs2, imm)
    }};
}
macro_rules! rd_rs {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $reg:ident) => {{
        let rs = Rs($reg(rs1($bit).try_into().unwrap()));
        let rd = Rd($reg(rd($bit).try_into().unwrap()));
        $type!($opcode1, $opcode2, rd, rs)
    }};
}
macro_rules! rs1_rs2 {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $reg:ident) => {{
        let rs1 = Rs1($reg(rs1($bit).try_into().unwrap()));
        let rs2 = Rs2($reg(rs2($bit).try_into().unwrap()));
        $type!($opcode1, $opcode2, rs1, rs2)
    }};
}
macro_rules! r {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $reg:ident) => {{
        let rd = Rd($reg(rd($bit).try_into().unwrap()));
        let rs1 = Rs1($reg(rs1($bit).try_into().unwrap()));
        let rs2 = Rs2($reg(rs2($bit).try_into().unwrap()));
        $type!($opcode1, $opcode2, rd, rs1, rs2)
    }};
}
macro_rules! s {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $reg:ident) => {{
        let rs1 = Rs1($reg(rs1($bit).try_into().unwrap()));
        let rs2 = Rs2($reg(rs2($bit).try_into().unwrap()));
        let imm = Imm32::<11, 0>::from(stype_immediate($bit));
        $type!($opcode1, $opcode2, rs1, rs2, imm)
    }};
}
macro_rules! j {
    ($type:ident,  $opcode1:ident, $opcode2:ident,  $bit:expr, $reg:ident) => {{
        let rd = Rd($reg(rd($bit).try_into().unwrap()));
        let imm = Imm32::<20, 1>::from(jtype_immediate($bit) as u32);
        $type!($opcode1, $opcode2, rd, imm)
    }};
}
macro_rules! u {
    ($type:ident,  $opcode1:ident, $opcode2:ident,  $bit:expr, $reg:ident) => {{
        let rd = Rd($reg(rd($bit).try_into().unwrap()));
        let imm = Imm32::<31, 12>::from(itype_immediate($bit) as u32);
        $type!($opcode1, $opcode2, rd, imm)
    }};
}
macro_rules! r_shamt {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $reg:ident) => {{
        let rd = Rd($reg(rd($bit).try_into().unwrap()));
        let rs1 = Rs1($reg(rs1($bit).try_into().unwrap()));
        let shamt = Shamt(shamt($bit).try_into().unwrap());
        $type!($opcode1, $opcode2, rd, rs1, shamt)
    }};
}
macro_rules! fence {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $reg:ident) => {{
        let rd = Rd($reg(rd($bit).try_into().unwrap()));
        let rs1 = Rs1($reg(rs1($bit).try_into().unwrap()));
        let succ = FenceSucc(Xx::new(($bit & 0x00F_00000) >> 20));
        let pred = FencePred(Xx::new(($bit & 0x0F0_00000) >> 24));
        let fm = FenceFm(Xx::new(($bit & 0xF00_00000) >> 28));
        $type!($opcode1, $opcode2, rd, rs1, succ, pred, fm)
    }};
}
macro_rules! zicsr_rs1 {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $reg:ident) => {{
        let rd = Rd($reg(rd($bit).try_into().unwrap()));
        let rs1 = Rs1($reg(rs1($bit).try_into().unwrap()));
        let imm = Imm32::<11, 0>::from(itype_immediate($bit) as u32);
        let csr = CSRAddr(imm);
        $type!($opcode1, $opcode2, rd, rs1, csr)
    }};
}
macro_rules! zicsr_uimm {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $reg:ident) => {{
        let rd = Rd($reg(rd($bit).try_into().unwrap()));
        let rs1 = Imm32::<4, 0>::from(rs1($bit) as u32);
        let uimm = UImm(rs1);
        let imm = Imm32::<11, 0>::from(itype_immediate($bit) as u32);
        let csr = CSRAddr(imm);
        $type!($opcode1, $opcode2, rd, uimm, csr)
    }};
}
macro_rules! ra {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $reg:ident) => {{
        let rd = Rd($reg(rd($bit).try_into().unwrap()));
        let rs1 = Rs1($reg(rs1($bit).try_into().unwrap()));
        let rs2 = Rs2($reg(rs2($bit).try_into().unwrap()));
        let aq = AQ(aq($bit));
        let rl = RL(rl($bit));
        $type!($opcode1, $opcode2, rd, rs1, rs2, aq, rl)
    }};
}
macro_rules! ra_only_rs1 {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $reg:ident) => {{
        let rd = Rd($reg(rd($bit).try_into().unwrap()));
        let rs1 = Rs1($reg(rs1($bit).try_into().unwrap()));
        let aq = AQ(aq($bit));
        let rl = RL(rl($bit));
        $type!($opcode1, $opcode2, rd, rs1, aq, rl)
    }};
}
macro_rules! rrm {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $rd_reg:ident, $rs_reg: ident) => {{
        let rd = Rd($rd_reg(rd($bit).try_into().unwrap()));
        let rs1 = Rs1($rs_reg(rs1($bit).try_into().unwrap()));
        let rs2 = Rs2($rs_reg(rs2($bit).try_into().unwrap()));
        let rm = funct3($bit) as u8;
        let rm = match rm {
            0b000 => RoundingMode::RNE,
            0b001 => RoundingMode::RTZ,
            0b010 => RoundingMode::RDN,
            0b011 => RoundingMode::RUP,
            0b100 => RoundingMode::RMM,
            0b111 => RoundingMode::DYN,
            _ => panic!("wrong RoundingMode"),
        };
        $type!($opcode1, $opcode2, rd, rs1, rs2, rm)
    }};
}
macro_rules! rrm_no_rs2 {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $rd_reg:ident, $rs_reg: ident) => {{
        let rd = Rd($rd_reg(rd($bit).try_into().unwrap()));
        let rs1 = Rs1($rs_reg(rs1($bit).try_into().unwrap()));
        let rm = funct3($bit) as u8;
        let rm = match rm {
            0b000 => RoundingMode::RNE,
            0b001 => RoundingMode::RTZ,
            0b010 => RoundingMode::RDN,
            0b011 => RoundingMode::RUP,
            0b100 => RoundingMode::RMM,
            0b111 => RoundingMode::DYN,
            _ => panic!("wrong RoundingMode"),
        };
        $type!($opcode1, $opcode2, rd, rs1, rm)
    }};
}
macro_rules! r4 {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr, $reg:ident) => {{
        let rd = Rd($reg(rd($bit).try_into().unwrap()));
        let rs1 = Rs1($reg(rs1($bit).try_into().unwrap()));
        let rs2 = Rs2($reg(rs2($bit).try_into().unwrap()));
        let rs3 = Rs3($reg(rs3($bit).try_into().unwrap()));
        let rm = funct3($bit) as u8;
        let rm = match rm {
            0b000 => RoundingMode::RNE,
            0b001 => RoundingMode::RTZ,
            0b010 => RoundingMode::RDN,
            0b011 => RoundingMode::RUP,
            0b100 => RoundingMode::RMM,
            0b111 => RoundingMode::DYN,
            _ => panic!("wrong RoundingMode"),
        };
        $type!($opcode1, $opcode2, rd, rs1, rs2, rs3, rm)
    }};
}
macro_rules! rrm_no_rm {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr,$rd_reg:ident, $rs_reg: ident) => {{
        let rd = Rd($rd_reg(rd($bit).try_into().unwrap()));
        let rs1 = Rs1($rs_reg(rs1($bit).try_into().unwrap()));
        let rs2 = Rs2($rs_reg(rs2($bit).try_into().unwrap()));
        $type!($opcode1, $opcode2, rd, rs1, rs2)
    }};
}
macro_rules! rrm_no_rs2_rm {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr,$rd_reg:ident, $rs_reg: ident) => {{
        let rd = Rd($rd_reg(rd($bit).try_into().unwrap()));
        let rs1 = Rs1($rs_reg(rs1($bit).try_into().unwrap()));
        $type!($opcode1, $opcode2, rd, rs1)
    }};
}
macro_rules! vx {
    ($type:ident, $opcode1:ident, $opcode2:ident, $bit:expr,$rd_reg:ident, $rs_reg: ident) => {{
        let rd = Rd($rd_reg(rd($bit).try_into().unwrap()));
        let rs1 = Rs1($rs_reg(rs1($bit).try_into().unwrap()));
        $type!($opcode1, $opcode2, rd, rs1)
    }};
}
impl Instruction {
    fn parse(bit: &[u8]) -> Instruction {
        if let Some(instr) = try_from_compressed(bit) {
            instr
        } else {
            let bit_u32 = u32::from_le_bytes(bit.split_at(4).0.try_into().unwrap());

            let opcode = slice(bit_u32, 0, 7, 0) as u16;

            let instr = match opcode {
                0b0110111 => Some(u!(rv32, RV32I, LUI, bit_u32, gp)),
                0b0010111 => Some(u!(rv32, RV32I, AUIPC, bit_u32, gp)),
                0b1101111 => Some(j!(rv32, RV32I, JAL, bit_u32, gp)),
                0b1100111 => {
                    match funct3(bit_u32) {
                        // I-type jump instructions
                        0b000 => Some(i!(rv32, RV32I, JALR, bit_u32, gp)),
                        _ => None,
                    }
                }
                0b0000011 => {
                    // I-type load instructions
                    match funct3(bit_u32) {
                        0b000 => Some(i!(rv32, RV32I, LB, bit_u32, gp)),
                        0b001 => Some(i!(rv32, RV32I, LH, bit_u32, gp)),
                        0b010 => Some(i!(rv32, RV32I, LW, bit_u32, gp)),
                        0b100 => Some(i!(rv32, RV32I, LBU, bit_u32, gp)),
                        0b101 => Some(i!(rv32, RV32I, LHU, bit_u32, gp)),
                        0b110 => Some(i!(rv64, RV64I, LWU, bit_u32, gp)),
                        0b011 => Some(i!(rv64, RV64I, LD, bit_u32, gp)),
                        _ => None,
                    }
                }
                0b0010011 => {
                    let funct3_val = funct3(bit_u32);

                    let inst = match funct3_val {
                        // I-type ALU instructions
                        0b000 => Some(i!(rv32, RV32I, ADDI, bit_u32, gp)),
                        0b010 => Some(i!(rv32, RV32I, SLTI, bit_u32, gp)),
                        0b011 => Some(i!(rv32, RV32I, SLTIU, bit_u32, gp)),
                        0b100 => Some(i!(rv32, RV32I, XORI, bit_u32, gp)),
                        0b110 => Some(i!(rv32, RV32I, ORI, bit_u32, gp)),
                        0b111 => Some(i!(rv32, RV32I, ANDI, bit_u32, gp)),
                        // I-type special ALU instructions
                        0b001 | 0b101 => {
                            let top6_val = funct7(bit_u32) >> 1;
                            match (funct3_val, top6_val) {
                                (0b001, 0b000000) => Some(r_shamt!(rv64, RV64I, SLLI, bit_u32, gp)),
                                (0b101, 0b000000) => Some(r_shamt!(rv64, RV64I, SRLI, bit_u32, gp)),
                                (0b101, 0b010000) => Some(r_shamt!(rv64, RV64I, SRAI, bit_u32, gp)),
                                _ => None,
                            }
                        }
                        _ => None,
                    };
                    if let Some(inst) = inst {
                        Some(inst)
                    } else {
                        let funct3_value = funct3(bit_u32);
                        let funct7_value = funct7(bit_u32);
                        let rs2_value = rs2(bit_u32);
                        let inst = match (funct7_value, funct3_value, rs2_value) {
                            (0b0010100, 0b101, 0b00111) => {
                                Some(r!(rv64_no_e, RVB, ORCB, bit_u32, gp))
                            }
                            (0b0110101, 0b101, 0b11000) => {
                                Some(rd_rs!(rv64_no_e, RVB, REV8, bit_u32, gp))
                            }
                            (0b0110000, 0b001, 0b00000) => {
                                Some(rd_rs!(rv64_no_e, RVB, CLZ, bit_u32, gp))
                            }
                            (0b0110000, 0b001, 0b00010) => {
                                Some(rd_rs!(rv64_no_e, RVB, CPOP, bit_u32, gp))
                            }
                            (0b0110000, 0b001, 0b00001) => {
                                Some(rd_rs!(rv64_no_e, RVB, CTZ, bit_u32, gp))
                            }
                            (0b0110000, 0b001, 0b00100) => {
                                Some(rd_rs!(rv64_no_e, RVB, SEXTB, bit_u32, gp))
                            }
                            (0b0110000, 0b001, 0b00101) => {
                                Some(rd_rs!(rv64_no_e, RVB, SEXTH, bit_u32, gp))
                            }
                            _ => None,
                        };
                        if let Some(inst) = inst {
                            Some(inst)
                        } else {
                            match (funct7_value >> 1, funct3_value) {
                                (0b010010, 0b001) => Some(i!(rv64_no_e, RVB, BCLRI, bit_u32, gp)),
                                (0b010010, 0b101) => Some(i!(rv64_no_e, RVB, BEXTI, bit_u32, gp)),
                                (0b011010, 0b001) => Some(i!(rv64_no_e, RVB, BINVI, bit_u32, gp)),
                                (0b001010, 0b001) => Some(i!(rv64_no_e, RVB, BSETI, bit_u32, gp)),
                                (0b011000, 0b101) => {
                                    Some(r_shamt!(rv64_no_e, RVB, RORI, bit_u32, gp))
                                }
                                _ => None,
                            }
                        }
                    }
                }
                0b1100011 => match funct3(bit_u32) {
                    0b000 => Some(b!(rv32, RV32I, BEQ, bit_u32, gp)),
                    0b001 => Some(b!(rv32, RV32I, BNE, bit_u32, gp)),
                    0b100 => Some(b!(rv32, RV32I, BLT, bit_u32, gp)),
                    0b101 => Some(b!(rv32, RV32I, BGE, bit_u32, gp)),
                    0b110 => Some(b!(rv32, RV32I, BLTU, bit_u32, gp)),
                    0b111 => Some(b!(rv32, RV32I, BGEU, bit_u32, gp)),
                    _ => None,
                },
                0b0100011 => match funct3(bit_u32) {
                    0b000 => Some(s!(rv32, RV32I, SB, bit_u32, gp)),
                    0b001 => Some(s!(rv32, RV32I, SH, bit_u32, gp)),
                    0b010 => Some(s!(rv32, RV32I, SW, bit_u32, gp)),
                    0b011 => Some(s!(rv64, RV64I, SD, bit_u32, gp)),
                    _ => None,
                },
                0b0110011 => {
                    let inst = match (funct3(bit_u32), funct7(bit_u32)) {
                        (0b000, 0b0000000) => Some(r!(rv32, RV32I, ADD, bit_u32, gp)),
                        (0b000, 0b0100000) => Some(r!(rv32, RV32I, SUB, bit_u32, gp)),
                        (0b001, 0b0000000) => Some(r!(rv32, RV32I, SLL, bit_u32, gp)),
                        (0b010, 0b0000000) => Some(r!(rv32, RV32I, SLT, bit_u32, gp)),
                        (0b011, 0b0000000) => Some(r!(rv32, RV32I, SLTU, bit_u32, gp)),
                        (0b100, 0b0000000) => Some(r!(rv32, RV32I, XOR, bit_u32, gp)),
                        (0b101, 0b0000000) => Some(r!(rv32, RV32I, SRL, bit_u32, gp)),
                        (0b101, 0b0100000) => Some(r!(rv32, RV32I, SRA, bit_u32, gp)),
                        (0b110, 0b0000000) => Some(r!(rv32, RV32I, OR, bit_u32, gp)),
                        (0b111, 0b0000000) => Some(r!(rv32, RV32I, AND, bit_u32, gp)),
                        (0b111, 0b0100000) => Some(r!(rv64_no_e, RVB, ANDN, bit_u32, gp)),
                        (0b110, 0b0100000) => Some(r!(rv64_no_e, RVB, ORN, bit_u32, gp)),
                        (0b100, 0b0100000) => Some(r!(rv64_no_e, RVB, XNOR, bit_u32, gp)),
                        (0b001, 0b0110000) => Some(r!(rv64_no_e, RVB, ROL, bit_u32, gp)),
                        (0b101, 0b0110000) => Some(r!(rv64_no_e, RVB, ROR, bit_u32, gp)),
                        (0b001, 0b0110100) => Some(r!(rv64_no_e, RVB, BINV, bit_u32, gp)),
                        (0b001, 0b0010100) => Some(r!(rv64_no_e, RVB, BSET, bit_u32, gp)),
                        (0b001, 0b0100100) => Some(r!(rv64_no_e, RVB, BCLR, bit_u32, gp)),
                        (0b101, 0b0100100) => Some(r!(rv64_no_e, RVB, BEXT, bit_u32, gp)),
                        (0b010, 0b0010000) => Some(r!(rv64_no_e, RVB, SH1ADD, bit_u32, gp)),
                        (0b100, 0b0010000) => Some(r!(rv64_no_e, RVB, SH2ADD, bit_u32, gp)),
                        (0b110, 0b0010000) => Some(r!(rv64_no_e, RVB, SH3ADD, bit_u32, gp)),
                        (0b001, 0b0000101) => Some(r!(rv64_no_e, RVB, CLMUL, bit_u32, gp)),
                        (0b011, 0b0000101) => Some(r!(rv64_no_e, RVB, CLMULH, bit_u32, gp)),
                        (0b010, 0b0000101) => Some(r!(rv64_no_e, RVB, CLMULR, bit_u32, gp)),
                        (0b100, 0b0000101) => Some(r!(rv64_no_e, RVB, MIN, bit_u32, gp)),
                        (0b101, 0b0000101) => Some(r!(rv64_no_e, RVB, MINU, bit_u32, gp)),
                        (0b110, 0b0000101) => Some(r!(rv64_no_e, RVB, MAX, bit_u32, gp)),
                        (0b111, 0b0000101) => Some(r!(rv64_no_e, RVB, MAXU, bit_u32, gp)),
                        _ => None,
                    };
                    if let Some(inst) = inst {
                        Some(inst)
                    } else {
                        match funct3(bit_u32) {
                            0b000 => Some(r!(rv32_no_e, RV32M, MUL, bit_u32, gp)),
                            0b001 => Some(r!(rv32_no_e, RV32M, MULH, bit_u32, gp)),
                            0b010 => Some(r!(rv32_no_e, RV32M, MULHSU, bit_u32, gp)),
                            0b011 => Some(r!(rv32_no_e, RV32M, MULHU, bit_u32, gp)),
                            0b100 => Some(r!(rv32_no_e, RV32M, DIV, bit_u32, gp)),
                            0b101 => Some(r!(rv32_no_e, RV32M, DIVU, bit_u32, gp)),
                            0b110 => Some(r!(rv32_no_e, RV32M, REM, bit_u32, gp)),
                            0b111 => Some(r!(rv32_no_e, RV32M, REMU, bit_u32, gp)),
                            _ => None,
                        }
                    }
                }
                0b0001111 => {
                    const FENCE_TSO: u32 = 0b1000_0011_0011_00000_000_00000_0001111;
                    const FENCE_PAUSE: u32 = 0b0000_0001_0000_00000_000_00000_0001111;
                    const FENCE_I: u32 = 0b0000_0000_0000_00000_001_00000_0001111;
                    match funct3(bit_u32) {
                        0b000 => match bit_u32 {
                            FENCE_TSO => Some(rv32!(RV32I, FENCE_TSO)),
                            FENCE_PAUSE => Some(rv32!(RV32I, PAUSE)),
                            _fence => Some(fence!(rv32, RV32I, FENCE, bit_u32, gp)),
                        },
                        0b001 => Some(i!(rv64_no_e, RVZifencei, FENCE_I, bit_u32, gp)),
                        _ => None,
                    }
                }
                0b1110011 => match funct3(bit_u32) {
                    0b000 => match itype_immediate(bit_u32) {
                        0b0 => Some(rv32!(RV32I, ECALL)),
                        0b1 => Some(rv32!(RV32I, EBREAK)),
                        _ => match funct7(bit_u32) {
                            0b0001000 => match rs2(bit_u32) as u8 {
                                0b00010 => Some(rv64_no_e!(RVPreviledge, SRET)),
                                0b00101 => Some(rv64_no_e!(RVPreviledge, WFI)),
                                _ => None,
                            },
                            0b0011000 => Some(rv64_no_e!(RVPreviledge, MRET)),
                            0b0001001 => {
                                Some(rs1_rs2!(rv64_no_e, RVPreviledge, SFENCE_VMA, bit_u32, gp))
                            }
                            0b0001011 => {
                                Some(rs1_rs2!(rv64_no_e, RVPreviledge, SINVAL_VMA, bit_u32, gp))
                            }
                            0b0001100 => match rs2(bit_u32) as u8 {
                                0b0 => Some(rv64_no_e!(RVPreviledge, SFENCE_W_INVAL)),
                                0b1 => Some(rv64_no_e!(RVPreviledge, SFENCE_INVAL_IR)),
                                _ => None,
                            },
                            _ => None,
                        },
                        _ => None,
                    },
                    0b001 => Some(zicsr_rs1!(rv64_no_e, RVZcsr, CSRRW, bit_u32, gp)),
                    0b010 => Some(zicsr_rs1!(rv64_no_e, RVZcsr, CSRRS, bit_u32, gp)),
                    0b011 => Some(zicsr_rs1!(rv64_no_e, RVZcsr, CSRRC, bit_u32, gp)),
                    0b101 => Some(zicsr_uimm!(rv64_no_e, RVZcsr, CSRRWI, bit_u32, gp)),
                    0b110 => Some(zicsr_uimm!(rv64_no_e, RVZcsr, CSRRSI, bit_u32, gp)),
                    0b111 => Some(zicsr_uimm!(rv64_no_e, RVZcsr, CSRRCI, bit_u32, gp)),
                    _ => None,
                },
                0b0101111 => match funct3(bit_u32) as u8 {
                    0b010 => match funct5(bit_u32) as u8 {
                        0b00010 => Some(ra_only_rs1!(rv32_no_e, RV32A, LR_W, bit_u32, gp)),
                        0b00011 => Some(ra!(rv32_no_e, RV32A, SC_W, bit_u32, gp)),
                        0b00001 => Some(ra!(rv32_no_e, RV32A, AMOSWAP_W, bit_u32, gp)),
                        0b00000 => Some(ra!(rv32_no_e, RV32A, AMOADD_W, bit_u32, gp)),
                        0b00100 => Some(ra!(rv32_no_e, RV32A, AMOXOR_W, bit_u32, gp)),
                        0b01100 => Some(ra!(rv32_no_e, RV32A, AMOAND_W, bit_u32, gp)),
                        0b01000 => Some(ra!(rv32_no_e, RV32A, AMOOR_W, bit_u32, gp)),
                        0b10000 => Some(ra!(rv32_no_e, RV32A, AMOMIN_W, bit_u32, gp)),
                        0b10100 => Some(ra!(rv32_no_e, RV32A, AMOMAX_W, bit_u32, gp)),
                        0b11000 => Some(ra!(rv32_no_e, RV32A, AMOMINU_W, bit_u32, gp)),
                        0b11100 => Some(ra!(rv32_no_e, RV32A, AMOMAXU_W, bit_u32, gp)),
                        _ => None,
                    },
                    0b011 => match funct5(bit_u32) as u8 {
                        0b00010 => Some(ra_only_rs1!(rv64_no_e, RV64A, LR_D, bit_u32, gp)),
                        0b00011 => Some(ra!(rv64_no_e, RV64A, SC_D, bit_u32, gp)),
                        0b00001 => Some(ra!(rv64_no_e, RV64A, AMOSWAP_D, bit_u32, gp)),
                        0b00000 => Some(ra!(rv64_no_e, RV64A, AMOADD_D, bit_u32, gp)),
                        0b00100 => Some(ra!(rv64_no_e, RV64A, AMOXOR_D, bit_u32, gp)),
                        0b01100 => Some(ra!(rv64_no_e, RV64A, AMOAND_D, bit_u32, gp)),
                        0b01000 => Some(ra!(rv64_no_e, RV64A, AMOOR_D, bit_u32, gp)),
                        0b10000 => Some(ra!(rv64_no_e, RV64A, AMOMIN_D, bit_u32, gp)),
                        0b10100 => Some(ra!(rv64_no_e, RV64A, AMOMAX_D, bit_u32, gp)),
                        0b11000 => Some(ra!(rv64_no_e, RV64A, AMOMINU_D, bit_u32, gp)),
                        0b11100 => Some(ra!(rv64_no_e, RV64A, AMOMAXU_D, bit_u32, gp)),
                        _ => None,
                    },
                    _ => None,
                },

                0b0111011 => {
                    let inst = match (funct3(bit_u32), funct7(bit_u32)) {
                        (0b000, 0b0000000) => Some(r!(rv64, RV64I, ADDW, bit_u32, gp)),
                        (0b000, 0b0100000) => Some(r!(rv64, RV64I, SUBW, bit_u32, gp)),
                        (0b001, 0b0000000) => Some(r!(rv64, RV64I, SLLW, bit_u32, gp)),
                        (0b101, 0b0000000) => Some(r!(rv64, RV64I, SRLW, bit_u32, gp)),
                        (0b101, 0b0100000) => Some(r!(rv64, RV64I, SRAW, bit_u32, gp)),
                        _ => {
                            let funct3_value = funct3(bit_u32);
                            let funct7_value = funct7(bit_u32);
                            match (funct3_value, funct7_value) {
                                (0b000, 0b0000100) => Some(r!(rv64_no_e, RVB, ADDUW, bit_u32, gp)),
                                (0b001, 0b0110000) => Some(r!(rv64_no_e, RVB, ROLW, bit_u32, gp)),
                                (0b010, 0b0010000) => {
                                    Some(r!(rv64_no_e, RVB, SH1ADDUW, bit_u32, gp))
                                }
                                (0b100, 0b0000100) => {
                                    if unsafe { BIT_LENGTH == 1 } && rs2(bit_u32) == 0 {
                                        Some(rd_rs!(rv64_no_e, RVB, ZEXTH, bit_u32, gp))
                                    } else {
                                        None
                                    }
                                }
                                (0b100, 0b0010000) => {
                                    Some(r!(rv64_no_e, RVB, SH2ADDUW, bit_u32, gp))
                                }
                                (0b101, 0b0110000) => Some(r!(rv64_no_e, RVB, RORW, bit_u32, gp)),
                                (0b110, 0b0010000) => {
                                    Some(r!(rv64_no_e, RVB, SH3ADDUW, bit_u32, gp))
                                }
                                _ => None,
                            }
                        }
                    };
                    if let Some(inst) = inst {
                        Some(inst)
                    } else {
                        match funct3(bit_u32) {
                            0b000 => Some(r!(rv64_no_e, RV64M, MULW, bit_u32, gp)),
                            0b100 => Some(r!(rv64_no_e, RV64M, DIVW, bit_u32, gp)),
                            0b101 => Some(r!(rv64_no_e, RV64M, DIVUW, bit_u32, gp)),
                            0b110 => Some(r!(rv64_no_e, RV64M, REMW, bit_u32, gp)),
                            0b111 => Some(r!(rv64_no_e, RV64M, REMUW, bit_u32, gp)),
                            _ => None,
                        }
                    }
                }

                0b0011011 => {
                    let funct3_value = funct3(bit_u32);
                    let funct7_value = funct7(bit_u32);
                    let rs2_value = rs2(bit_u32);
                    let inst = match funct3_value {
                        0b000 => Some(i!(rv64, RV64I, ADDIW, bit_u32, gp)),
                        0b001 | 0b101 => {
                            let funct7_value = funct7(bit_u32);
                            match (funct3_value, funct7_value) {
                                (0b001, 0b0000000) => {
                                    Some(r_shamt!(rv64, RV64I, SLLIW, bit_u32, gp))
                                }
                                (0b101, 0b0000000) => {
                                    Some(r_shamt!(rv64, RV64I, SRLIW, bit_u32, gp))
                                }
                                (0b101, 0b0100000) => {
                                    Some(r_shamt!(rv64, RV64I, SRAIW, bit_u32, gp))
                                }
                                _ => None,
                            }
                        }
                        _ => None,
                    };
                    if let Some(inst) = inst {
                        Some(inst)
                    } else {
                        match funct7_value {
                            0b0110000 => match funct3_value {
                                0b001 => match rs2_value {
                                    0b00000 => Some(rd_rs!(rv64_no_e, RVB, CLZW, bit_u32, gp)),
                                    0b00010 => Some(rd_rs!(rv64_no_e, RVB, CPOPW, bit_u32, gp)),
                                    0b00001 => Some(rd_rs!(rv64_no_e, RVB, CTZW, bit_u32, gp)),
                                    _ => None,
                                },
                                0b101 => Some(r_shamt!(rv64_no_e, RVB, RORIW, bit_u32, gp)),
                                _ => None,
                            },
                            _ => {
                                if funct7_value >> 1 == 0b000010 && funct3_value == 0b001 {
                                    Some(r!(rv64_no_e, RVB, SLLIUW, bit_u32, gp))
                                } else {
                                    None
                                }
                            }
                        }
                    }
                }
                0b1010011 => match funct7(bit_u32) as u8 {
                    0b0000000 => Some(rrm!(rv32_no_e, RV32F, FADD_S, bit_u32, fp, fp)),
                    0b0000001 => Some(rrm!(rv32_no_e, RV32D, FADD_D, bit_u32, fp, fp)),
                    0b0000100 => Some(rrm!(rv32_no_e, RV32F, FSUB_S, bit_u32, fp, fp)),
                    0b0000101 => Some(rrm!(rv32_no_e, RV32D, FSUB_D, bit_u32, fp, fp)),
                    0b0001000 => Some(rrm!(rv32_no_e, RV32F, FMUL_S, bit_u32, fp, fp)),
                    0b0001001 => Some(rrm!(rv32_no_e, RV32D, FMUL_D, bit_u32, fp, fp)),
                    0b0001100 => Some(rrm!(rv32_no_e, RV32F, FDIV_S, bit_u32, fp, fp)),
                    0b0001101 => Some(rrm!(rv32_no_e, RV32D, FDIV_D, bit_u32, fp, fp)),
                    0b0101100 => Some(rrm_no_rs2!(rv32_no_e, RV32F, FSQRT_S, bit_u32, fp, fp)),
                    0b0101101 => Some(rrm_no_rs2!(rv32_no_e, RV32D, FSQRT_D, bit_u32, fp, fp)),
                    0b0010000 => match funct3(bit_u32) as u8 {
                        0b000 => Some(rrm_no_rm!(rv32_no_e, RV32F, FSGNJ_S, bit_u32, fp, fp)),
                        0b001 => Some(rrm_no_rm!(rv32_no_e, RV32F, FSGNJN_S, bit_u32, fp, fp)),
                        0b010 => Some(rrm_no_rm!(rv32_no_e, RV32F, FSGNJX_S, bit_u32, fp, fp)),
                        _ => None,
                    },
                    0b0010001 => match funct3(bit_u32) as u8 {
                        0b000 => Some(rrm_no_rm!(rv32_no_e, RV32D, FSGNJ_D, bit_u32, fp, fp)),
                        0b001 => Some(rrm_no_rm!(rv32_no_e, RV32D, FSGNJN_D, bit_u32, fp, fp)),
                        0b010 => Some(rrm_no_rm!(rv32_no_e, RV32D, FSGNJX_D, bit_u32, fp, fp)),
                        _ => None,
                    },
                    0b0010100 => match funct3(bit_u32) as u8 {
                        0b000 => Some(rrm_no_rm!(rv32_no_e, RV32F, FMIN_S, bit_u32, fp, fp)),
                        0b001 => Some(rrm_no_rm!(rv32_no_e, RV32F, FMAX_S, bit_u32, fp, fp)),
                        _ => None,
                    },
                    0b0010101 => match funct3(bit_u32) as u8 {
                        0b000 => Some(rrm_no_rm!(rv32_no_e, RV32D, FMIN_D, bit_u32, fp, fp)),
                        0b001 => Some(rrm_no_rm!(rv32_no_e, RV32D, FMAX_D, bit_u32, fp, fp)),
                        _ => None,
                    },
                    0b1100000 => match rs2(bit_u32) as u8 {
                        0b00000 => Some(rrm_no_rs2!(rv32_no_e, RV32F, FCVT_W_S, bit_u32, gp, fp)),
                        0b00001 => Some(rrm_no_rs2!(rv32_no_e, RV32F, FCVT_WU_S, bit_u32, gp, fp)),
                        0b00010 => Some(rrm_no_rs2!(rv64_no_e, RV64F, FCVT_L_S, bit_u32, gp, fp)),
                        0b00011 => Some(rrm_no_rs2!(rv64_no_e, RV64F, FCVT_LU_S, bit_u32, gp, fp)),
                        _ => None,
                    },
                    0b1110000 => match rs2(bit_u32) as u8 {
                        0b00000 => match funct3(bit_u32) as u8 {
                            0b000 => {
                                Some(rrm_no_rs2_rm!(rv32_no_e, RV32F, FMV_X_W, bit_u32, gp, fp))
                            }
                            0b001 => {
                                Some(rrm_no_rs2_rm!(rv32_no_e, RV32F, FCLASS_S, bit_u32, gp, fp))
                            }
                            _ => None,
                        },
                        _ => None,
                    },
                    0b0100000 => match rs2(bit_u32) as u8 {
                        0b00001 => Some(rrm_no_rs2!(rv32_no_e, RV32D, FCVT_S_D, bit_u32, fp, fp)),
                        _ => None,
                    },
                    0b0100001 => match rs2(bit_u32) as u8 {
                        0b00000 => Some(rrm_no_rs2!(rv32_no_e, RV32D, FCVT_D_S, bit_u32, fp, fp)),
                        _ => None,
                    },
                    0b1010000 => match funct3(bit_u32) as u8 {
                        0b010 => Some(rrm_no_rm!(rv32_no_e, RV32F, FEQ_S, bit_u32, gp, fp)),
                        0b001 => Some(rrm_no_rm!(rv32_no_e, RV32F, FLT_S, bit_u32, gp, fp)),
                        0b000 => Some(rrm_no_rm!(rv32_no_e, RV32F, FLE_S, bit_u32, gp, fp)),
                        _ => None,
                    },
                    0b1010001 => match funct3(bit_u32) as u8 {
                        0b010 => Some(rrm_no_rm!(rv32_no_e, RV32D, FEQ_D, bit_u32, gp, fp)),
                        0b001 => Some(rrm_no_rm!(rv32_no_e, RV32D, FLT_D, bit_u32, gp, fp)),
                        0b000 => Some(rrm_no_rm!(rv32_no_e, RV32D, FLE_D, bit_u32, gp, fp)),
                        _ => None,
                    },
                    0b1101000 => match rs2(bit_u32) as u8 {
                        0b00000 => Some(rrm_no_rs2!(rv32_no_e, RV32F, FCVT_S_W, bit_u32, fp, gp)),
                        0b00001 => Some(rrm_no_rs2!(rv32_no_e, RV32F, FCVT_S_WU, bit_u32, fp, gp)),
                        0b00010 => Some(rrm_no_rs2!(rv64_no_e, RV64F, FCVT_S_L, bit_u32, fp, gp)),
                        0b00011 => Some(rrm_no_rs2!(rv64_no_e, RV64F, FCVT_S_LU, bit_u32, fp, gp)),
                        _ => None,
                    },
                    0b1111000 => match (rs2(bit_u32) as u8, funct3(bit_u32) as u8) {
                        (0b00000, 0b000) => {
                            Some(rrm_no_rs2_rm!(rv32_no_e, RV32F, FMV_W_X, bit_u32, fp, gp))
                        }
                        _ => None,
                    },
                    0b1111001 => match (rs2(bit_u32) as u8, funct3(bit_u32) as u8) {
                        (0b00000, 0b000) => {
                            Some(rrm_no_rs2_rm!(rv64_no_e, RV64D, FMV_D_X, bit_u32, fp, gp))
                        }
                        _ => None,
                    },
                    0b1110001 => match (rs2(bit_u32) as u8, funct3(bit_u32) as u8) {
                        (0b00000, 0b000) => {
                            Some(rrm_no_rs2_rm!(rv64_no_e, RV64D, FMV_X_D, bit_u32, gp, fp))
                        }
                        (0b00000, 0b001) => {
                            Some(rrm_no_rs2_rm!(rv32_no_e, RV32D, FCLASS_D, bit_u32, gp, fp))
                        }
                        _ => None,
                    },
                    0b1100001 => match rs2(bit_u32) as u8 {
                        0b00000 => Some(rrm_no_rs2!(rv32_no_e, RV32D, FCVT_W_D, bit_u32, gp, fp)),
                        0b00001 => Some(rrm_no_rs2!(rv32_no_e, RV32D, FCVT_WU_D, bit_u32, gp, fp)),
                        0b00010 => Some(rrm_no_rs2!(rv64_no_e, RV64D, FCVT_L_D, bit_u32, gp, fp)),
                        0b00011 => Some(rrm_no_rs2!(rv64_no_e, RV64D, FCVT_LU_D, bit_u32, gp, fp)),
                        _ => None,
                    },
                    0b1101001 => match rs2(bit_u32) as u8 {
                        0b00000 => Some(rrm_no_rs2!(rv32_no_e, RV32D, FCVT_D_W, bit_u32, fp, gp)),
                        0b00001 => Some(rrm_no_rs2!(rv32_no_e, RV32D, FCVT_D_WU, bit_u32, fp, gp)),
                        0b00010 => Some(rrm_no_rs2!(rv64_no_e, RV64D, FCVT_D_L, bit_u32, fp, gp)),
                        0b00011 => Some(rrm_no_rs2!(rv64_no_e, RV64D, FCVT_D_LU, bit_u32, fp, gp)),
                        _ => None,
                    },
                    _ => None,
                },
                0b1000011 => match funct2(bit_u32) as u8 {
                    0b00 => Some(r4!(rv32_no_e, RV32F, FMADD_S, bit_u32, fp)),
                    0b01 => Some(r4!(rv32_no_e, RV32D, FMADD_D, bit_u32, fp)),
                    _ => None,
                },
                0b1000111 => match funct2(bit_u32) as u8 {
                    0b00 => Some(r4!(rv32_no_e, RV32F, FMSUB_S, bit_u32, fp)),
                    0b01 => Some(r4!(rv32_no_e, RV32D, FMSUB_D, bit_u32, fp)),
                    _ => None,
                },
                0b1001111 => match funct2(bit_u32) as u8 {
                    0b00 => Some(r4!(rv32_no_e, RV32F, FNMADD_S, bit_u32, fp)),
                    0b01 => Some(r4!(rv32_no_e, RV32D, FNMADD_D, bit_u32, fp)),
                    _ => None,
                },
                0b1001011 => match funct2(bit_u32) as u8 {
                    0b00 => Some(r4!(rv32_no_e, RV32F, FNMSUB_S, bit_u32, fp)),
                    0b01 => Some(r4!(rv32_no_e, RV32D, FNMSUB_D, bit_u32, fp)),
                    _ => None,
                },
                0b0000111 => {
                    #[rustfmt::skip]
                    match instruction_bits {
                        x if x & 0b11111111111100000111000001111111 == 0b00000010101100000000000000000111 => Some(VLM_V),
                        x if x & 0b00011101111100000111000001111111 == 0b00000000000000000000000000000111 => Some(VLE8_V),
                        x if x & 0b00011101111100000111000001111111 == 0b00000000000000000101000000000111 => Some(VLE16_V),
                        x if x & 0b00011101111100000111000001111111 == 0b00000000000000000110000000000111 => Some(VLE32_V),
                        x if x & 0b00011101111100000111000001111111 == 0b00000000000000000111000000000111 => Some(VLE64_V),
                        x if x & 0b00011101111100000111000001111111 == 0b00010000000000000000000000000111 => Some(VLE128_V),
                        x if x & 0b00011101111100000111000001111111 == 0b00010000000000000101000000000111 => Some(VLE256_V),
                        x if x & 0b00011101111100000111000001111111 == 0b00010000000000000110000000000111 => Some(VLE512_V),
                        x if x & 0b00011101111100000111000001111111 == 0b00010000000000000111000000000111 => Some(VLE1024_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00001000000000000000000000000111 => Some(VLSE8_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00001000000000000101000000000111 => Some(VLSE16_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00001000000000000110000000000111 => Some(VLSE32_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00001000000000000111000000000111 => Some(VLSE64_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00011000000000000000000000000111 => Some(VLSE128_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00011000000000000101000000000111 => Some(VLSE256_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00011000000000000110000000000111 => Some(VLSE512_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00011000000000000111000000000111 => Some(VLSE1024_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00000100000000000000000000000111 => Some(VLUXEI8_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00000100000000000101000000000111 => Some(VLUXEI16_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00000100000000000110000000000111 => Some(VLUXEI32_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00000100000000000111000000000111 => Some(VLUXEI64_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00010100000000000000000000000111 => Some(VLUXEI128_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00010100000000000101000000000111 => Some(VLUXEI256_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00010100000000000110000000000111 => Some(VLUXEI512_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00010100000000000111000000000111 => Some(VLUXEI1024_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00001100000000000000000000000111 => Some(VLOXEI8_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00001100000000000101000000000111 => Some(VLOXEI16_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00001100000000000110000000000111 => Some(VLOXEI32_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00001100000000000111000000000111 => Some(VLOXEI64_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00011100000000000000000000000111 => Some(VLOXEI128_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00011100000000000101000000000111 => Some(VLOXEI256_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00011100000000000110000000000111 => Some(VLOXEI512_V),
                        x if x & 0b00011100000000000111000001111111 == 0b00011100000000000111000000000111 => Some(VLOXEI1024_V),
                        x if x & 0b11111111111100000111000001111111 == 0b00000010100000000000000000000111 => Some(VL1RE8_V),
                        x if x & 0b11111111111100000111000001111111 == 0b00000010100000000101000000000111 => Some(VL1RE16_V),
                        x if x & 0b11111111111100000111000001111111 == 0b00000010100000000110000000000111 => Some(VL1RE32_V),
                        x if x & 0b11111111111100000111000001111111 == 0b00000010100000000111000000000111 => Some(VL1RE64_V),
                        x if x & 0b11111111111100000111000001111111 == 0b00100010100000000000000000000111 => Some(VL2RE8_V),
                        x if x & 0b11111111111100000111000001111111 == 0b00100010100000000101000000000111 => Some(VL2RE16_V),
                        x if x & 0b11111111111100000111000001111111 == 0b00100010100000000110000000000111 => Some(VL2RE32_V),
                        x if x & 0b11111111111100000111000001111111 == 0b00100010100000000111000000000111 => Some(VL2RE64_V),
                        x if x & 0b11111111111100000111000001111111 == 0b01100010100000000000000000000111 => Some(VL4RE8_V),
                        x if x & 0b11111111111100000111000001111111 == 0b01100010100000000101000000000111 => Some(VL4RE16_V),
                        x if x & 0b11111111111100000111000001111111 == 0b01100010100000000110000000000111 => Some(VL4RE32_V),
                        x if x & 0b11111111111100000111000001111111 == 0b01100010100000000111000000000111 => Some(VL4RE64_V),
                        x if x & 0b11111111111100000111000001111111 == 0b_11100010100000000000000000000111 => Some(VL8RE8_V),
                        x if x & 0b11111111111100000111000001111111 == 0b_11100010100000000101000000000111 => Some(VL8RE16_V),
                        x if x & 0b11111111111100000111000001111111 == 0b_11100010100000000110000000000111 => Some(VL8RE32_V),
                        x if x & 0b11111111111100000111000001111111 == 0b_11100010100000000111000000000111 => Some(VL8RE64_V),
                        _ => None,
                    }
                },
                // 0b0100111 => {
                //             #[rustfmt::skip]
                //             match bit_u32 {
                //                 x if x & 0b11111111111100000111000001111111 == 0b00000010101100000000000000100111 => Some(VSM_V),
                //                 x if x & 0b00011101111100000111000001111111 == 0b00000000000000000000000000100111 => Some(VSE8_V),
                //                 x if x & 0b00011101111100000111000001111111 == 0b00000000000000000101000000100111 => Some(VSE16_V),
                //                 x if x & 0b00011101111100000111000001111111 == 0b00000000000000000110000000100111 => Some(VSE32_V),
                //                 x if x & 0b00011101111100000111000001111111 == 0b00000000000000000111000000100111 => Some(VSE64_V),
                //                 x if x & 0b00011101111100000111000001111111 == 0b00010000000000000000000000100111 => Some(VSE128_V),
                //                 x if x & 0b00011101111100000111000001111111 == 0b00010000000000000101000000100111 => Some(VSE256_V),
                //                 x if x & 0b00011101111100000111000001111111 == 0b00010000000000000110000000100111 => Some(VSE512_V),
                //                 x if x & 0b00011101111100000111000001111111 == 0b00010000000000000111000000100111 => Some(VSE1024_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00001000000000000000000000100111 => Some(VSSE8_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00001000000000000101000000100111 => Some(VSSE16_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00001000000000000110000000100111 => Some(VSSE32_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00001000000000000111000000100111 => Some(VSSE64_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00011000000000000000000000100111 => Some(VSSE128_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00011000000000000101000000100111 => Some(VSSE256_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00011000000000000110000000100111 => Some(VSSE512_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00011000000000000111000000100111 => Some(VSSE1024_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00000100000000000000000000100111 => Some(VSUXEI8_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00000100000000000101000000100111 => Some(VSUXEI16_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00000100000000000110000000100111 => Some(VSUXEI32_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00000100000000000111000000100111 => Some(VSUXEI64_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00010100000000000000000000100111 => Some(VSUXEI128_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00010100000000000101000000100111 => Some(VSUXEI256_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00010100000000000110000000100111 => Some(VSUXEI512_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00010100000000000111000000100111 => Some(VSUXEI1024_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00001100000000000000000000100111 => Some(VSOXEI8_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00001100000000000101000000100111 => Some(VSOXEI16_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00001100000000000110000000100111 => Some(VSOXEI32_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00001100000000000111000000100111 => Some(VSOXEI64_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00011100000000000000000000100111 => Some(VSOXEI128_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00011100000000000101000000100111 => Some(VSOXEI256_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00011100000000000110000000100111 => Some(VSOXEI512_V),
                //                 x if x & 0b00011100000000000111000001111111 == 0b00011100000000000111000000100111 => Some(VSOXEI1024_V),
                //                 x if x & 0b11111111111100000111000001111111 == 0b00000010100000000000000000100111 => Some(VS1R_V),
                //                 x if x & 0b11111111111100000111000001111111 == 0b00100010100000000000000000100111 => Some(VS2R_V),
                //                 x if x & 0b11111111111100000111000001111111 == 0b01100010100000000000000000100111 => Some(VS4R_V),
                //                 x if x & 0b11111111111100000111000001111111 == 0b11100010100000000000000000100111 => Some(VS8R_V),
                //                 _ => None,
                //             }
                //         }
                // 0b1010111 => match funct3(bit_u32) {
                //     0b000 => {
                //         #[rustfmt::skip]
                //         let inst_opt = match bit_u32 {
                //             x if x & 0b11111100000000000111000001111111 == 0b00000000000000000000000001010111 => Some(VADD_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b00001000000000000000000001010111 => Some(VSUB_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b00010000000000000000000001010111 => Some(VMINU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b00010100000000000000000001010111 => Some(VMIN_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b00011000000000000000000001010111 => Some(VMAXU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b00011100000000000000000001010111 => Some(VMAX_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b01100000000000000000000001010111 => Some(VMSEQ_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b01100100000000000000000001010111 => Some(VMSNE_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b01101000000000000000000001010111 => Some(VMSLTU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b01101100000000000000000001010111 => Some(VMSLT_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b01110000000000000000000001010111 => Some(VMSLEU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b01110100000000000000000001010111 => Some(VMSLE_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10010100000000000000000001010111 => Some(VSLL_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10100000000000000000000001010111 => Some(VSRL_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10100100000000000000000001010111 => Some(VSRA_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b00100100000000000000000001010111 => Some(VAND_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b00101000000000000000000001010111 => Some(VOR_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b00101100000000000000000001010111 => Some(VXOR_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10000000000000000000000001010111 => Some(VSADDU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10000100000000000000000001010111 => Some(VSADD_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10001000000000000000000001010111 => Some(VSSUBU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10001100000000000000000001010111 => Some(VSSUB_VV),
                //             x if x & 0b11111111111100000111000001111111 == 0b01011110000000000000000001010111 => Some(VMV_V_V),
                //             x if x & 0b11111100000000000111000001111111 == 0b10110000000000000000000001010111 => Some(VNSRL_WV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10110100000000000000000001010111 => Some(VNSRA_WV),
                //             x if x & 0b11111110000000000111000001111111 == 0b01000110000000000000000001010111 => Some(VMADC_VV),
                //             x if x & 0b11111110000000000111000001111111 == 0b01001110000000000000000001010111 => Some(VMSBC_VV),
                //             x if x & 0b11111110000000000111000001111111 == 0b01000000000000000000000001010111 => Some(VADC_VVM),
                //             x if x & 0b11111110000000000111000001111111 == 0b01001000000000000000000001010111 => Some(VSBC_VVM),
                //             x if x & 0b11111100000000000111000001111111 == 0b01000100000000000000000001010111 => Some(VMADC_VVM),
                //             x if x & 0b11111100000000000111000001111111 == 0b01001100000000000000000001010111 => Some(VMSBC_VVM),
                //             x if x & 0b11111100000000000111000001111111 == 0b10101000000000000000000001010111 => Some(VSSRL_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10101100000000000000000001010111 => Some(VSSRA_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10011100000000000000000001010111 => Some(VSMUL_VV),
                //             x if x & 0b11111110000000000111000001111111 == 0b01011100000000000000000001010111 => Some(VMERGE_VVM),
                //             x if x & 0b11111100000000000111000001111111 == 0b10111000000000000000000001010111 => Some(VNCLIPU_WV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10111100000000000000000001010111 => Some(VNCLIP_WV),
                //             x if x & 0b11111100000000000111000001111111 == 0b11000000000000000000000001010111 => Some(VWREDSUMU_VS),
                //             x if x & 0b11111100000000000111000001111111 == 0b11000100000000000000000001010111 => Some(VWREDSUM_VS),
                //             x if x & 0b11111100000000000111000001111111 == 0b00110000000000000000000001010111 => Some(VRGATHER_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b00111000000000000000000001010111 => Some(VRGATHEREI16_VV),
                //             _ => None,
                //         };
                //         inst_opt.map(|inst| {
                //             VVtype::new(
                //                 inst,
                //                 rd(bit_u32),
                //                 rs1(bit_u32),
                //                 rs2(bit_u32),
                //                 vm(bit_u32),
                //             )
                //             .0
                //         })
                //     }
                //     0b010 => {
                //         #[rustfmt::skip]
                //         let inst_opt = match bit_u32 {
                //             x if x & 0b11111100000000000111000001111111 == 0b10000000000000000010000001010111 => Some(VDIVU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10001000000000000010000001010111 => Some(VREMU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10000100000000000010000001010111 => Some(VDIV_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10001100000000000010000001010111 => Some(VREM_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10010100000000000010000001010111 => Some(VMUL_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10011100000000000010000001010111 => Some(VMULH_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10010000000000000010000001010111 => Some(VMULHU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10011000000000000010000001010111 => Some(VMULHSU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b00100100000000000010000001010111 => Some(VAADD_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b00100000000000000010000001010111 => Some(VAADDU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b00101100000000000010000001010111 => Some(VASUB_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b00101000000000000010000001010111 => Some(VASUBU_VV),
                //             x if x & 0b11111100000011111111000001111111 == 0b01000000000010001010000001010111 => Some(VFIRST_M),
                //             x if x & 0b11111100000011111111000001111111 == 0b01000000000010000010000001010111 => Some(VCPOP_M),
                //             x if x & 0b11111100000000000111000001111111 == 0b11000000000000000010000001010111 => Some(VWADDU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b11000100000000000010000001010111 => Some(VWADD_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b11001000000000000010000001010111 => Some(VWSUBU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b11001100000000000010000001010111 => Some(VWSUB_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b11010000000000000010000001010111 => Some(VWADDU_WV),
                //             x if x & 0b11111100000000000111000001111111 == 0b11010100000000000010000001010111 => Some(VWADD_WV),
                //             x if x & 0b11111100000000000111000001111111 == 0b11011000000000000010000001010111 => Some(VWSUBU_WV),
                //             x if x & 0b11111100000000000111000001111111 == 0b11011100000000000010000001010111 => Some(VWSUB_WV),
                //             x if x & 0b11111100000000000111000001111111 == 0b11100000000000000010000001010111 => Some(VWMULU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b11101000000000000010000001010111 => Some(VWMULSU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b11101100000000000010000001010111 => Some(VWMUL_VV),
                //             x if x & 0b11111100000011111111000001111111 == 0b01001000000000110010000001010111 => Some(VZEXT_VF2),
                //             x if x & 0b11111100000011111111000001111111 == 0b01001000000000100010000001010111 => Some(VZEXT_VF4),
                //             x if x & 0b11111100000011111111000001111111 == 0b01001000000000010010000001010111 => Some(VZEXT_VF8),
                //             x if x & 0b11111100000011111111000001111111 == 0b01001000000000111010000001010111 => Some(VSEXT_VF2),
                //             x if x & 0b11111100000011111111000001111111 == 0b01001000000000101010000001010111 => Some(VSEXT_VF4),
                //             x if x & 0b11111100000011111111000001111111 == 0b01001000000000011010000001010111 => Some(VSEXT_VF8),
                //             x if x & 0b11111100000000000111000001111111 == 0b01100000000000000010000001010111 => Some(VMANDNOT_MM),
                //             x if x & 0b11111100000000000111000001111111 == 0b01100100000000000010000001010111 => Some(VMAND_MM),
                //             x if x & 0b11111100000000000111000001111111 == 0b01101000000000000010000001010111 => Some(VMOR_MM),
                //             x if x & 0b11111100000000000111000001111111 == 0b01101100000000000010000001010111 => Some(VMXOR_MM),
                //             x if x & 0b11111100000000000111000001111111 == 0b01110000000000000010000001010111 => Some(VMORNOT_MM),
                //             x if x & 0b11111100000000000111000001111111 == 0b01110100000000000010000001010111 => Some(VMNAND_MM),
                //             x if x & 0b11111100000000000111000001111111 == 0b01111000000000000010000001010111 => Some(VMNOR_MM),
                //             x if x & 0b11111100000000000111000001111111 == 0b01111100000000000010000001010111 => Some(VMXNOR_MM),
                //             x if x & 0b11111100000000000111000001111111 == 0b10110100000000000010000001010111 => Some(VMACC_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10111100000000000010000001010111 => Some(VNMSAC_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10100100000000000010000001010111 => Some(VMADD_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b10101100000000000010000001010111 => Some(VNMSUB_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b11110000000000000010000001010111 => Some(VWMACCU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b11110100000000000010000001010111 => Some(VWMACC_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b11111100000000000010000001010111 => Some(VWMACCSU_VV),
                //             x if x & 0b11111100000000000111000001111111 == 0b00000000000000000010000001010111 => Some(VREDSUM_VS),
                //             x if x & 0b11111100000000000111000001111111 == 0b00000100000000000010000001010111 => Some(VREDAND_VS),
                //             x if x & 0b11111100000000000111000001111111 == 0b00001000000000000010000001010111 => Some(VREDOR_VS),
                //             x if x & 0b11111100000000000111000001111111 == 0b00001100000000000010000001010111 => Some(VREDXOR_VS),
                //             x if x & 0b11111100000000000111000001111111 == 0b00010000000000000010000001010111 => Some(VREDMINU_VS),
                //             x if x & 0b11111100000000000111000001111111 == 0b00010100000000000010000001010111 => Some(VREDMIN_VS),
                //             x if x & 0b11111100000000000111000001111111 == 0b00011000000000000010000001010111 => Some(VREDMAXU_VS),
                //             x if x & 0b11111100000000000111000001111111 == 0b00011100000000000010000001010111 => Some(VREDMAX_VS),
                //             x if x & 0b11111100000011111111000001111111 == 0b01010000000000001010000001010111 => Some(VMSBF_M),
                //             x if x & 0b11111100000011111111000001111111 == 0b01010000000000011010000001010111 => Some(VMSIF_M),
                //             x if x & 0b11111100000011111111000001111111 == 0b01010000000000010010000001010111 => Some(VMSOF_M),
                //             x if x & 0b11111100000011111111000001111111 == 0b01010000000010000010000001010111 => Some(VIOTA_M),
                //             x if x & 0b11111101111111111111000001111111 == 0b01010000000010001010000001010111 => Some(VID_V),
                //             x if x & 0b11111110000011111111000001111111 == 0b01000010000000000010000001010111 => Some(VMV_X_S),
                //             x if x & 0b11111100000000000111000001111111 == 0b01011100000000000010000001010111 => Some(VCOMPRESS_VM),
                //             _ => None,
                //         };
                //         inst_opt.map(|inst| {
                //             VVtype::new(
                //                 inst,
                //                 rd(bit_u32),
                //                 rs1(bit_u32),
                //                 rs2(bit_u32),
                //                 vm(bit_u32),
                //             )
                //             .0
                //         })
                //     }
                //     0b011 => {
                //         #[rustfmt::skip]
                //         let inst_opt = match bit_u32 {
                //             x if x & 0b11111100000000000111000001111111 == 0b00000000000000000011000001010111 => Some(VADD_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b00001100000000000011000001010111 => Some(VRSUB_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b01100000000000000011000001010111 => Some(VMSEQ_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b01100100000000000011000001010111 => Some(VMSNE_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b01110000000000000011000001010111 => Some(VMSLEU_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b01110100000000000011000001010111 => Some(VMSLE_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b01111000000000000011000001010111 => Some(VMSGTU_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b01111100000000000011000001010111 => Some(VMSGT_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b10010100000000000011000001010111 => Some(VSLL_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b10100000000000000011000001010111 => Some(VSRL_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b10100100000000000011000001010111 => Some(VSRA_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b00100100000000000011000001010111 => Some(VAND_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b00101000000000000011000001010111 => Some(VOR_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b00101100000000000011000001010111 => Some(VXOR_VI),
                //             x if x & 0b11111110000011111111000001111111 == 0b10011110000000000011000001010111 => Some(VMV1R_V),
                //             x if x & 0b11111110000011111111000001111111 == 0b10011110000000001011000001010111 => Some(VMV2R_V),
                //             x if x & 0b11111110000011111111000001111111 == 0b10011110000000011011000001010111 => Some(VMV4R_V),
                //             x if x & 0b11111110000011111111000001111111 == 0b10011110000000111011000001010111 => Some(VMV8R_V),
                //             x if x & 0b11111100000000000111000001111111 == 0b10000000000000000011000001010111 => Some(VSADDU_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b10000100000000000011000001010111 => Some(VSADD_VI),
                //             x if x & 0b11111111111100000111000001111111 == 0b01011110000000000011000001010111 => Some(VMV_V_I),
                //             x if x & 0b11111100000000000111000001111111 == 0b10110000000000000011000001010111 => Some(VNSRL_WI),
                //             x if x & 0b11111100000000000111000001111111 == 0b10110100000000000011000001010111 => Some(VNSRA_WI),
                //             x if x & 0b11111110000000000111000001111111 == 0b01000110000000000011000001010111 => Some(VMADC_VI),
                //             x if x & 0b11111110000000000111000001111111 == 0b01000000000000000011000001010111 => Some(VADC_VIM),
                //             x if x & 0b11111100000000000111000001111111 == 0b01000100000000000011000001010111 => Some(VMADC_VIM),
                //             x if x & 0b11111100000000000111000001111111 == 0b10101000000000000011000001010111 => Some(VSSRL_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b10101100000000000011000001010111 => Some(VSSRA_VI),
                //             x if x & 0b11111110000000000111000001111111 == 0b01011100000000000011000001010111 => Some(VMERGE_VIM),
                //             x if x & 0b11111100000000000111000001111111 == 0b10111000000000000011000001010111 => Some(VNCLIPU_WI),
                //             x if x & 0b11111100000000000111000001111111 == 0b10111100000000000011000001010111 => Some(VNCLIP_WI),
                //             x if x & 0b11111100000000000111000001111111 == 0b00111000000000000011000001010111 => Some(VSLIDEUP_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b00111100000000000011000001010111 => Some(VSLIDEDOWN_VI),
                //             x if x & 0b11111100000000000111000001111111 == 0b00110000000000000011000001010111 => Some(VRGATHER_VI),
                //             _ => None,
                //         };
                //         inst_opt.map(|inst| {
                //             VItype::new(
                //                 inst,
                //                 rd(bit_u32),
                //                 rs2(bit_u32),
                //                 utils::x(bit_u32, 15, 5, 0),
                //                 vm(bit_u32),
                //             )
                //             .0
                //         })
                //     }
                //     0b100 => {
                //         #[rustfmt::skip]
                //         let inst_opt = match bit_u32 {
                //             x if x & 0b11111100000000000111000001111111 == 0b00000000000000000100000001010111 => Some(VADD_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00001000000000000100000001010111 => Some(VSUB_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00001100000000000100000001010111 => Some(VRSUB_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00010000000000000100000001010111 => Some(VMINU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00010100000000000100000001010111 => Some(VMIN_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00011000000000000100000001010111 => Some(VMAXU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00011100000000000100000001010111 => Some(VMAX_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b01100000000000000100000001010111 => Some(VMSEQ_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b01100100000000000100000001010111 => Some(VMSNE_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b01101000000000000100000001010111 => Some(VMSLTU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b01101100000000000100000001010111 => Some(VMSLT_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b01110000000000000100000001010111 => Some(VMSLEU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b01110100000000000100000001010111 => Some(VMSLE_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b01111000000000000100000001010111 => Some(VMSGTU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b01111100000000000100000001010111 => Some(VMSGT_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10010100000000000100000001010111 => Some(VSLL_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10100000000000000100000001010111 => Some(VSRL_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10100100000000000100000001010111 => Some(VSRA_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00100100000000000100000001010111 => Some(VAND_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00101000000000000100000001010111 => Some(VOR_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00101100000000000100000001010111 => Some(VXOR_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10000000000000000100000001010111 => Some(VSADDU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10000100000000000100000001010111 => Some(VSADD_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10001000000000000100000001010111 => Some(VSSUBU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10001100000000000100000001010111 => Some(VSSUB_VX),
                //             x if x & 0b11111111111100000111000001111111 == 0b01011110000000000100000001010111 => Some(VMV_V_X),
                //             x if x & 0b11111100000000000111000001111111 == 0b10110000000000000100000001010111 => Some(VNSRL_WX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10110100000000000100000001010111 => Some(VNSRA_WX),
                //             x if x & 0b11111110000000000111000001111111 == 0b01000110000000000100000001010111 => Some(VMADC_VX),
                //             x if x & 0b11111110000000000111000001111111 == 0b01001110000000000100000001010111 => Some(VMSBC_VX),
                //             x if x & 0b11111110000000000111000001111111 == 0b01000000000000000100000001010111 => Some(VADC_VXM),
                //             x if x & 0b11111110000000000111000001111111 == 0b01001000000000000100000001010111 => Some(VSBC_VXM),
                //             x if x & 0b11111100000000000111000001111111 == 0b01000100000000000100000001010111 => Some(VMADC_VXM),
                //             x if x & 0b11111100000000000111000001111111 == 0b01001100000000000100000001010111 => Some(VMSBC_VXM),
                //             x if x & 0b11111100000000000111000001111111 == 0b10101000000000000100000001010111 => Some(VSSRL_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10101100000000000100000001010111 => Some(VSSRA_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10011100000000000100000001010111 => Some(VSMUL_VX),
                //             x if x & 0b11111110000000000111000001111111 == 0b01011100000000000100000001010111 => Some(VMERGE_VXM),
                //             x if x & 0b11111100000000000111000001111111 == 0b10111000000000000100000001010111 => Some(VNCLIPU_WX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10111100000000000100000001010111 => Some(VNCLIP_WX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00111100000000000100000001010111 => Some(VSLIDEDOWN_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00111000000000000100000001010111 => Some(VSLIDEUP_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00110000000000000100000001010111 => Some(VRGATHER_VX),
                //             _ => None,
                //         };
                //         inst_opt.map(|inst| {
                //             VXtype::new(
                //                 inst,
                //                 rd(bit_u32),
                //                 rs1(bit_u32),
                //                 rs2(bit_u32),
                //                 vm(bit_u32),
                //             )
                //             .0
                //         })
                //     }
                //     0b110 => {
                //         #[rustfmt::skip]
                //         let inst_opt = match bit_u32 {
                //             x if x & 0b11111100000000000111000001111111 == 0b10000000000000000110000001010111 => Some(VDIVU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10000100000000000110000001010111 => Some(VDIV_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10001000000000000110000001010111 => Some(VREMU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10001100000000000110000001010111 => Some(VREM_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10010100000000000110000001010111 => Some(VMUL_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10011100000000000110000001010111 => Some(VMULH_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10010000000000000110000001010111 => Some(VMULHU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10011000000000000110000001010111 => Some(VMULHSU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11000000000000000110000001010111 => Some(VWADDU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11000100000000000110000001010111 => Some(VWADD_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11001000000000000110000001010111 => Some(VWSUBU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11100000000000000110000001010111 => Some(VWMULU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11101000000000000110000001010111 => Some(VWMULSU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11101100000000000110000001010111 => Some(VWMUL_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11001100000000000110000001010111 => Some(VWSUB_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11010000000000000110000001010111 => Some(VWADDU_WX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11010100000000000110000001010111 => Some(VWADD_WX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11011000000000000110000001010111 => Some(VWSUBU_WX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11011100000000000110000001010111 => Some(VWSUB_WX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00100100000000000110000001010111 => Some(VAADD_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00100000000000000110000001010111 => Some(VAADDU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00101100000000000110000001010111 => Some(VASUB_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00101000000000000110000001010111 => Some(VASUBU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10110100000000000110000001010111 => Some(VMACC_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10111100000000000110000001010111 => Some(VNMSAC_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10100100000000000110000001010111 => Some(VMADD_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b10101100000000000110000001010111 => Some(VNMSUB_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11110000000000000110000001010111 => Some(VWMACCU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11110100000000000110000001010111 => Some(VWMACC_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11111100000000000110000001010111 => Some(VWMACCSU_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b11111000000000000110000001010111 => Some(VWMACCUS_VX),
                //             x if x & 0b11111111111100000111000001111111 == 0b01000010000000000110000001010111 => Some(VMV_S_X),
                //             x if x & 0b11111100000000000111000001111111 == 0b00111000000000000110000001010111 => Some(VSLIDE1UP_VX),
                //             x if x & 0b11111100000000000111000001111111 == 0b00111100000000000110000001010111 => Some(VSLIDE1DOWN_VX),
                //             _ => None,
                //         };
                //         inst_opt.map(|inst| {
                //             VXtype::new(
                //                 inst,
                //                 rd(bit_u32),
                //                 rs1(bit_u32),
                //                 rs2(bit_u32),
                //                 vm(bit_u32),
                //             )
                //             .0
                //         })
                //     }
                //     0b111 => {
                //         #[rustfmt::skip]
                //         let r = match bit_u32 {
                //             x if x & 0b10000000000000000111000001111111 == 0b00000000000000000111000001010111 => Some(Itype::new_u(VSETVLI, rd(instruction_bits), rs1(instruction_bits), utils::x(instruction_bits, 20, 11, 0)).0),
                //             x if x & 0b11000000000000000111000001111111 == 0b11000000000000000111000001010111 => Som1e(Itype::new_u(VSETIVLI, rd(instruction_bits), rs1(instruction_bits), utils::x(instruction_bits, 20, 10, 0)).0),
                //             x if x & 0b11111110000000000111000001111111 == 0b10000000000000000111000001010111 => Some(Rtype::new(VSETVL, rd(instruction_bits), rs1(instruction_bits), rs2(instruction_bits)).0),
                //             _ => None,
                //         };
                //         r
                //     }
                //     _ => None,
                // },
                _ => None,
            }
            .unwrap();

            dbg!(opcode);
            Self { instr }
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
        // Add assertion for incorect machine
        // match instruction.instr {
        //     Instr::RV32(_) => unsafe { assert_eq!(BIT_LENGTH, 0) },
        //     Instr::RV64(_) => unsafe { assert_eq!(BIT_LENGTH, 1) },
        //     Instr::RV128(_) => unsafe { assert_eq!(BIT_LENGTH, 2) },
        //     Instr::NOP => {}
        // }
        Some((address, instruction))
    }
}

#[cfg(test)]
mod tests {
    use crate::frontend::instruction::*;

    #[test]
    fn test_rvi_addi() {
        let instr_asm: u32 = 0b11101101100000011000000110010011;
        // let instr_asm: u32 = 0b11011000010111;
        let instr = Instruction::parse(&instr_asm.to_le_bytes());
        dbg!(instr.clone());
        assert_eq!(
            instr.instr,
            Instr::RV32(RV32Instr::RV32I(RV32I::ADDI(
                Rd(Reg::X(Xx(3))),
                Rs1(Reg::X(Xx(3))),
                Imm32::<11, 0>::from(4294967000)
            )))
        );
    }
    #[test]
    fn test_rvi_auipc() {
        let instr_asm: u32 = 0b11011000010111;
        let instr = Instruction::parse(&instr_asm.to_le_bytes());
        dbg!(instr.clone());
        assert_eq!(
            instr.instr,
            Instr::RV32(RV32Instr::RV32I(RV32I::AUIPC(
                Rd(Reg::X(Xx(12))),
                Imm32::<31, 12>::from(0)
            )))
        );
    }
    #[test]
    fn test_rvb_clzw() {
        let instr_asm: u32 = 0b1100000000001010001110000011011;
        let instr = Instruction::parse(&instr_asm.to_le_bytes());
        dbg!(instr.clone());
        assert_eq!(
            instr.instr,
            Instr::RV64(RV64Instr::RVB(RVB::CLZW(
                Rd(Reg::X(Xx(24))),
                Rs(Reg::X(Xx(10))),
            )))
        );
    }
    #[test]
    fn test_rvs_sd() {
        let instr_asm: u32 = 0b100010111100010011110000100011;
        let instr = Instruction::parse(&instr_asm.to_le_bytes());
        dbg!(instr.clone());
        assert_eq!(
            instr.instr,
            Instr::RV64(RV64Instr::RV64I(RV64I::SD(
                Rs1(Reg::X(Xx(2))),
                Rs2(Reg::X(Xx(15))),
                Imm32::<11, 0>::from(568),
            )))
        );
    }
    #[test]
    fn test_pause() {
        let instr_asm: u32 = 0b00000001000000000000000000001111;
        let instr = Instruction::parse(&instr_asm.to_le_bytes());
        dbg!(instr.clone());
        assert_eq!(instr.instr, Instr::RV32(RV32Instr::RV32I(RV32I::PAUSE)));
    }
    #[test]
    fn test_rva_amoadd() {
        let instr_asm: u32 = 0b101001000011010010101111;
        let instr = Instruction::parse(&instr_asm.to_le_bytes());
        dbg!(instr.clone());
        assert_eq!(
            instr.instr,
            Instr::RV64(RV64Instr::RV64A(RV64A::AMOADD_D(
                Rd(Reg::X(Xx::new(9))),
                Rs1(Reg::X(Xx::new(8))),
                Rs2(Reg::X(Xx::new(10))),
                AQ(false),
                RL(false)
            )))
        );
    }
    #[test]
    fn test_rvi_shamt_srai() {
        let instr_asm: u32 = 0b1000011100001111101010010010011;
        let instr = Instruction::parse(&instr_asm.to_le_bytes());
        dbg!(instr.clone());
        assert_eq!(
            instr.instr,
            Instr::RV64(RV64Instr::RV64I(RV64I::SRAI(
                Rd(Reg::X(Xx::new(9))),
                Rs1(Reg::X(Xx::new(15))),
                Shamt(24)
            )))
        );
    }
    #[test]
    fn test_rvp_sfence() {
        let instr_asm: u32 = 0b10010000000000000000001110011;
        let instr = Instruction::parse(&instr_asm.to_le_bytes());
        dbg!(instr.clone());
        assert_eq!(
            instr.instr,
            Instr::RV64(RV64Instr::RVPreviledge(RVPreviledge::SFENCE_VMA(
                Rs1(Reg::X(Xx::new(0))),
                Rs2(Reg::X(Xx::new(0)))
            )))
        );
    }
}
