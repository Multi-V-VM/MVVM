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
            Reg::X(ref x) => write!(f, "a{}", x.0),
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

#[allow(non_snake_case)]
#[allow(non_upper_case_globals)]
mod OpCode {
    // IMC
    pub const OP_UNLOADED: u16 = 0x00;
    pub const OP_ADD: u16 = 0x01;
    pub const OP_ADDI: u16 = 0x02;
    pub const OP_ADDIW: u16 = 0x03;
    pub const OP_ADDW: u16 = 0x04;
    pub const OP_AND: u16 = 0x05;
    pub const OP_ANDI: u16 = 0x06;
    pub const OP_DIV: u16 = 0x07;
    pub const OP_DIVU: u16 = 0x08;
    pub const OP_DIVUW: u16 = 0x09;
    pub const OP_DIVW: u16 = 0x0a;
    pub const OP_FENCE: u16 = 0x0b;
    pub const OP_FENCEI: u16 = 0x0c;
    pub const OP_LB: u16 = 0x0d;
    pub const OP_LBU: u16 = 0x0e;
    pub const OP_LD: u16 = 0x0f;
    pub const OP_LH: u16 = 0x10;
    pub const OP_LHU: u16 = 0x11;
    pub const OP_LUI: u16 = 0x12;
    pub const OP_LW: u16 = 0x13;
    pub const OP_LWU: u16 = 0x14;
    pub const OP_MUL: u16 = 0x15;
    pub const OP_MULH: u16 = 0x16;
    pub const OP_MULHSU: u16 = 0x17;
    pub const OP_MULHU: u16 = 0x18;
    pub const OP_MULW: u16 = 0x19;
    pub const OP_OR: u16 = 0x1a;
    pub const OP_ORI: u16 = 0x1b;
    pub const OP_REM: u16 = 0x1c;
    pub const OP_REMU: u16 = 0x1d;
    pub const OP_REMUW: u16 = 0x1e;
    pub const OP_REMW: u16 = 0x1f;
    pub const OP_SB: u16 = 0x20;
    pub const OP_SD: u16 = 0x21;
    pub const OP_SH: u16 = 0x22;
    pub const OP_SLL: u16 = 0x23;
    pub const OP_SLLI: u16 = 0x24;
    pub const OP_SLLIW: u16 = 0x25;
    pub const OP_SLLW: u16 = 0x26;
    pub const OP_SLT: u16 = 0x27;
    pub const OP_SLTI: u16 = 0x28;
    pub const OP_SLTIU: u16 = 0x29;
    pub const OP_SLTU: u16 = 0x2a;
    pub const OP_SRA: u16 = 0x2b;
    pub const OP_SRAI: u16 = 0x2c;
    pub const OP_SRAIW: u16 = 0x2d;
    pub const OP_SRAW: u16 = 0x2e;
    pub const OP_SRL: u16 = 0x2f;
    pub const OP_SRLI: u16 = 0x30;
    pub const OP_SRLIW: u16 = 0x31;
    pub const OP_SRLW: u16 = 0x32;
    pub const OP_SUB: u16 = 0x33;
    pub const OP_SUBW: u16 = 0x34;
    pub const OP_SW: u16 = 0x35;
    pub const OP_XOR: u16 = 0x36;
    pub const OP_XORI: u16 = 0x37;
    // B
    pub const OP_ADDUW: u16 = 0x38;
    pub const OP_ANDN: u16 = 0x39;
    pub const OP_BCLR: u16 = 0x3a;
    pub const OP_BCLRI: u16 = 0x3b;
    pub const OP_BEXT: u16 = 0x3c;
    pub const OP_BEXTI: u16 = 0x3d;
    pub const OP_BINV: u16 = 0x3e;
    pub const OP_BINVI: u16 = 0x3f;
    pub const OP_BSET: u16 = 0x40;
    pub const OP_BSETI: u16 = 0x41;
    pub const OP_CLMUL: u16 = 0x42;
    pub const OP_CLMULH: u16 = 0x43;
    pub const OP_CLMULR: u16 = 0x44;
    pub const OP_CLZ: u16 = 0x45;
    pub const OP_CLZW: u16 = 0x46;
    pub const OP_CPOP: u16 = 0x47;
    pub const OP_CPOPW: u16 = 0x48;
    pub const OP_CTZ: u16 = 0x49;
    pub const OP_CTZW: u16 = 0x4a;
    pub const OP_MAX: u16 = 0x4b;
    pub const OP_MAXU: u16 = 0x4c;
    pub const OP_MIN: u16 = 0x4d;
    pub const OP_MINU: u16 = 0x4e;
    pub const OP_ORCB: u16 = 0x4f;
    pub const OP_ORN: u16 = 0x50;
    pub const OP_REV8: u16 = 0x51;
    pub const OP_ROL: u16 = 0x52;
    pub const OP_ROLW: u16 = 0x53;
    pub const OP_ROR: u16 = 0x54;
    pub const OP_RORI: u16 = 0x55;
    pub const OP_RORIW: u16 = 0x56;
    pub const OP_RORW: u16 = 0x57;
    pub const OP_SEXTB: u16 = 0x58;
    pub const OP_SEXTH: u16 = 0x59;
    pub const OP_SH1ADD: u16 = 0x5a;
    pub const OP_SH1ADDUW: u16 = 0x5b;
    pub const OP_SH2ADD: u16 = 0x5c;
    pub const OP_SH2ADDUW: u16 = 0x5d;
    pub const OP_SH3ADD: u16 = 0x5e;
    pub const OP_SH3ADDUW: u16 = 0x5f;
    pub const OP_SLLIUW: u16 = 0x60;
    pub const OP_XNOR: u16 = 0x61;
    pub const OP_ZEXTH: u16 = 0x62;
    // Mop
    pub const OP_WIDE_MUL: u16 = 0x63;
    pub const OP_WIDE_MULU: u16 = 0x64;
    pub const OP_WIDE_MULSU: u16 = 0x65;
    pub const OP_WIDE_DIV: u16 = 0x66;
    pub const OP_WIDE_DIVU: u16 = 0x67;
    pub const OP_ADC: u16 = 0x68;
    pub const OP_SBB: u16 = 0x69;
    pub const OP_CUSTOM_LOAD_UIMM: u16 = 0x6a;
    pub const OP_CUSTOM_LOAD_IMM: u16 = 0x6b;
    // Basic block ends
    pub const OP_AUIPC: u16 = 0x6c;
    pub const OP_BEQ: u16 = 0x6d;
    pub const OP_BGE: u16 = 0x6e;
    pub const OP_BGEU: u16 = 0x6f;
    pub const OP_BLT: u16 = 0x70;
    pub const OP_BLTU: u16 = 0x71;
    pub const OP_BNE: u16 = 0x72;
    pub const OP_EBREAK: u16 = 0x73;
    pub const OP_ECALL: u16 = 0x74;
    pub const OP_JAL: u16 = 0x75;
    pub const OP_JALR: u16 = 0x76;
    pub const OP_FAR_JUMP_REL: u16 = 0x77;
    pub const OP_FAR_JUMP_ABS: u16 = 0x78;
    pub const OP_CUSTOM_TRACE_END: u16 = 0x79;
    // V
    pub const OP_VSETVLI: u16 = 0x007a;
    pub const OP_VSETIVLI: u16 = 0x007b;
    pub const OP_VSETVL: u16 = 0x007c;
    pub const OP_VLM_V: u16 = 0x007d;
    pub const OP_VLE8_V: u16 = 0x007e;
    pub const OP_VLE16_V: u16 = 0x007f;
    pub const OP_VLE32_V: u16 = 0x0080;
    pub const OP_VLE64_V: u16 = 0x0081;
    pub const OP_VLE128_V: u16 = 0x0082;
    pub const OP_VLE256_V: u16 = 0x0083;
    pub const OP_VLE512_V: u16 = 0x0084;
    pub const OP_VLE1024_V: u16 = 0x0085;
    pub const OP_VSM_V: u16 = 0x0086;
    pub const OP_VSE8_V: u16 = 0x0087;
    pub const OP_VSE16_V: u16 = 0x0088;
    pub const OP_VSE32_V: u16 = 0x0089;
    pub const OP_VSE64_V: u16 = 0x008a;
    pub const OP_VSE128_V: u16 = 0x008b;
    pub const OP_VSE256_V: u16 = 0x008c;
    pub const OP_VSE512_V: u16 = 0x008d;
    pub const OP_VSE1024_V: u16 = 0x008e;
    pub const OP_VADD_VV: u16 = 0x008f;
    pub const OP_VADD_VX: u16 = 0x0090;
    pub const OP_VADD_VI: u16 = 0x0091;
    pub const OP_VSUB_VV: u16 = 0x0092;
    pub const OP_VSUB_VX: u16 = 0x0093;
    pub const OP_VRSUB_VX: u16 = 0x0094;
    pub const OP_VRSUB_VI: u16 = 0x0095;
    pub const OP_VMUL_VV: u16 = 0x0096;
    pub const OP_VMUL_VX: u16 = 0x0097;
    pub const OP_VDIV_VV: u16 = 0x0098;
    pub const OP_VDIV_VX: u16 = 0x0099;
    pub const OP_VDIVU_VV: u16 = 0x009a;
    pub const OP_VDIVU_VX: u16 = 0x009b;
    pub const OP_VREM_VV: u16 = 0x009c;
    pub const OP_VREM_VX: u16 = 0x009d;
    pub const OP_VREMU_VV: u16 = 0x009e;
    pub const OP_VREMU_VX: u16 = 0x009f;
    pub const OP_VSLL_VV: u16 = 0x00a0;
    pub const OP_VSLL_VX: u16 = 0x00a1;
    pub const OP_VSLL_VI: u16 = 0x00a2;
    pub const OP_VSRL_VV: u16 = 0x00a3;
    pub const OP_VSRL_VX: u16 = 0x00a4;
    pub const OP_VSRL_VI: u16 = 0x00a5;
    pub const OP_VSRA_VV: u16 = 0x00a6;
    pub const OP_VSRA_VX: u16 = 0x00a7;
    pub const OP_VSRA_VI: u16 = 0x00a8;
    pub const OP_VMSEQ_VV: u16 = 0x00a9;
    pub const OP_VMSEQ_VX: u16 = 0x00aa;
    pub const OP_VMSEQ_VI: u16 = 0x00ab;
    pub const OP_VMSNE_VV: u16 = 0x00ac;
    pub const OP_VMSNE_VX: u16 = 0x00ad;
    pub const OP_VMSNE_VI: u16 = 0x00ae;
    pub const OP_VMSLTU_VV: u16 = 0x00af;
    pub const OP_VMSLTU_VX: u16 = 0x00b0;
    pub const OP_VMSLT_VV: u16 = 0x00b1;
    pub const OP_VMSLT_VX: u16 = 0x00b2;
    pub const OP_VMSLEU_VV: u16 = 0x00b3;
    pub const OP_VMSLEU_VX: u16 = 0x00b4;
    pub const OP_VMSLEU_VI: u16 = 0x00b5;
    pub const OP_VMSLE_VV: u16 = 0x00b6;
    pub const OP_VMSLE_VX: u16 = 0x00b7;
    pub const OP_VMSLE_VI: u16 = 0x00b8;
    pub const OP_VMSGTU_VX: u16 = 0x00b9;
    pub const OP_VMSGTU_VI: u16 = 0x00ba;
    pub const OP_VMSGT_VX: u16 = 0x00bb;
    pub const OP_VMSGT_VI: u16 = 0x00bc;
    pub const OP_VMINU_VV: u16 = 0x00bd;
    pub const OP_VMINU_VX: u16 = 0x00be;
    pub const OP_VMIN_VV: u16 = 0x00bf;
    pub const OP_VMIN_VX: u16 = 0x00c0;
    pub const OP_VMAXU_VV: u16 = 0x00c1;
    pub const OP_VMAXU_VX: u16 = 0x00c2;
    pub const OP_VMAX_VV: u16 = 0x00c3;
    pub const OP_VMAX_VX: u16 = 0x00c4;
    pub const OP_VWADDU_VV: u16 = 0x00c5;
    pub const OP_VWADDU_VX: u16 = 0x00c6;
    pub const OP_VWSUBU_VV: u16 = 0x00c7;
    pub const OP_VWSUBU_VX: u16 = 0x00c8;
    pub const OP_VWADD_VV: u16 = 0x00c9;
    pub const OP_VWADD_VX: u16 = 0x00ca;
    pub const OP_VWSUB_VV: u16 = 0x00cb;
    pub const OP_VWSUB_VX: u16 = 0x00cc;
    pub const OP_VWADDU_WV: u16 = 0x00cd;
    pub const OP_VWADDU_WX: u16 = 0x00ce;
    pub const OP_VWSUBU_WV: u16 = 0x00cf;
    pub const OP_VWSUBU_WX: u16 = 0x00d0;
    pub const OP_VWADD_WV: u16 = 0x00d1;
    pub const OP_VWADD_WX: u16 = 0x00d2;
    pub const OP_VWSUB_WV: u16 = 0x00d3;
    pub const OP_VWSUB_WX: u16 = 0x00d4;
    pub const OP_VZEXT_VF8: u16 = 0x00d5;
    pub const OP_VSEXT_VF8: u16 = 0x00d6;
    pub const OP_VZEXT_VF4: u16 = 0x00d7;
    pub const OP_VSEXT_VF4: u16 = 0x00d8;
    pub const OP_VZEXT_VF2: u16 = 0x00d9;
    pub const OP_VSEXT_VF2: u16 = 0x00da;
    pub const OP_VADC_VVM: u16 = 0x00db;
    pub const OP_VADC_VXM: u16 = 0x00dc;
    pub const OP_VADC_VIM: u16 = 0x00dd;
    pub const OP_VMADC_VVM: u16 = 0x00de;
    pub const OP_VMADC_VXM: u16 = 0x00df;
    pub const OP_VMADC_VIM: u16 = 0x00e0;
    pub const OP_VMADC_VV: u16 = 0x00e1;
    pub const OP_VMADC_VX: u16 = 0x00e2;
    pub const OP_VMADC_VI: u16 = 0x00e3;
    pub const OP_VSBC_VVM: u16 = 0x00e4;
    pub const OP_VSBC_VXM: u16 = 0x00e5;
    pub const OP_VMSBC_VVM: u16 = 0x00e6;
    pub const OP_VMSBC_VXM: u16 = 0x00e7;
    pub const OP_VMSBC_VV: u16 = 0x00e8;
    pub const OP_VMSBC_VX: u16 = 0x00e9;
    pub const OP_VAND_VV: u16 = 0x00ea;
    pub const OP_VAND_VI: u16 = 0x00eb;
    pub const OP_VAND_VX: u16 = 0x00ec;
    pub const OP_VOR_VV: u16 = 0x00ed;
    pub const OP_VOR_VX: u16 = 0x00ee;
    pub const OP_VOR_VI: u16 = 0x00ef;
    pub const OP_VXOR_VV: u16 = 0x00f0;
    pub const OP_VXOR_VX: u16 = 0x00f1;
    pub const OP_VXOR_VI: u16 = 0x00f2;
    pub const OP_VNSRL_WV: u16 = 0x00f3;
    pub const OP_VNSRL_WX: u16 = 0x00f4;
    pub const OP_VNSRL_WI: u16 = 0x00f5;
    pub const OP_VNSRA_WV: u16 = 0x00f6;
    pub const OP_VNSRA_WX: u16 = 0x00f7;
    pub const OP_VNSRA_WI: u16 = 0x00f8;
    pub const OP_VMULH_VV: u16 = 0x00f9;
    pub const OP_VMULH_VX: u16 = 0x00fa;
    pub const OP_VMULHU_VV: u16 = 0x00fb;
    pub const OP_VMULHU_VX: u16 = 0x00fc;
    pub const OP_VMULHSU_VV: u16 = 0x00fd;
    pub const OP_VMULHSU_VX: u16 = 0x00fe;
    pub const OP_VWMULU_VV: u16 = 0x00ff;
    pub const OP_VWMULU_VX: u16 = 0x0100;
    pub const OP_VWMULSU_VV: u16 = 0x0101;
    pub const OP_VWMULSU_VX: u16 = 0x0102;
    pub const OP_VWMUL_VV: u16 = 0x0103;
    pub const OP_VWMUL_VX: u16 = 0x0104;
    pub const OP_VMV_V_V: u16 = 0x0105;
    pub const OP_VMV_V_X: u16 = 0x0106;
    pub const OP_VMV_V_I: u16 = 0x0107;
    pub const OP_VSADDU_VV: u16 = 0x0108;
    pub const OP_VSADDU_VX: u16 = 0x0109;
    pub const OP_VSADDU_VI: u16 = 0x010a;
    pub const OP_VSADD_VV: u16 = 0x010b;
    pub const OP_VSADD_VX: u16 = 0x010c;
    pub const OP_VSADD_VI: u16 = 0x010d;
    pub const OP_VSSUBU_VV: u16 = 0x010e;
    pub const OP_VSSUBU_VX: u16 = 0x010f;
    pub const OP_VSSUB_VV: u16 = 0x0110;
    pub const OP_VSSUB_VX: u16 = 0x0111;
    pub const OP_VAADDU_VV: u16 = 0x0112;
    pub const OP_VAADDU_VX: u16 = 0x0113;
    pub const OP_VAADD_VV: u16 = 0x0114;
    pub const OP_VAADD_VX: u16 = 0x0115;
    pub const OP_VASUBU_VV: u16 = 0x0116;
    pub const OP_VASUBU_VX: u16 = 0x0117;
    pub const OP_VASUB_VV: u16 = 0x0118;
    pub const OP_VASUB_VX: u16 = 0x0119;
    pub const OP_VMV1R_V: u16 = 0x011a;
    pub const OP_VMV2R_V: u16 = 0x011b;
    pub const OP_VMV4R_V: u16 = 0x011c;
    pub const OP_VMV8R_V: u16 = 0x011d;
    pub const OP_VFIRST_M: u16 = 0x011e;
    pub const OP_VMAND_MM: u16 = 0x011f;
    pub const OP_VMNAND_MM: u16 = 0x0120;
    pub const OP_VMANDNOT_MM: u16 = 0x0121;
    pub const OP_VMXOR_MM: u16 = 0x0122;
    pub const OP_VMOR_MM: u16 = 0x0123;
    pub const OP_VMNOR_MM: u16 = 0x0124;
    pub const OP_VMORNOT_MM: u16 = 0x0125;
    pub const OP_VMXNOR_MM: u16 = 0x0126;
    pub const OP_VLSE8_V: u16 = 0x0127;
    pub const OP_VLSE16_V: u16 = 0x0128;
    pub const OP_VLSE32_V: u16 = 0x0129;
    pub const OP_VLSE64_V: u16 = 0x012a;
    pub const OP_VLSE128_V: u16 = 0x012b;
    pub const OP_VLSE256_V: u16 = 0x012c;
    pub const OP_VLSE512_V: u16 = 0x012d;
    pub const OP_VLSE1024_V: u16 = 0x012e;
    pub const OP_VSSE8_V: u16 = 0x012f;
    pub const OP_VSSE16_V: u16 = 0x0130;
    pub const OP_VSSE32_V: u16 = 0x0131;
    pub const OP_VSSE64_V: u16 = 0x0132;
    pub const OP_VSSE128_V: u16 = 0x0133;
    pub const OP_VSSE256_V: u16 = 0x0134;
    pub const OP_VSSE512_V: u16 = 0x0135;
    pub const OP_VSSE1024_V: u16 = 0x0136;
    pub const OP_VLUXEI8_V: u16 = 0x0137;
    pub const OP_VLUXEI16_V: u16 = 0x0138;
    pub const OP_VLUXEI32_V: u16 = 0x0139;
    pub const OP_VLUXEI64_V: u16 = 0x013a;
    pub const OP_VLUXEI128_V: u16 = 0x013b;
    pub const OP_VLUXEI256_V: u16 = 0x013c;
    pub const OP_VLUXEI512_V: u16 = 0x013d;
    pub const OP_VLUXEI1024_V: u16 = 0x013e;
    pub const OP_VLOXEI8_V: u16 = 0x013f;
    pub const OP_VLOXEI16_V: u16 = 0x0140;
    pub const OP_VLOXEI32_V: u16 = 0x0141;
    pub const OP_VLOXEI64_V: u16 = 0x0142;
    pub const OP_VLOXEI128_V: u16 = 0x0143;
    pub const OP_VLOXEI256_V: u16 = 0x0144;
    pub const OP_VLOXEI512_V: u16 = 0x0145;
    pub const OP_VLOXEI1024_V: u16 = 0x0146;
    pub const OP_VSUXEI8_V: u16 = 0x0147;
    pub const OP_VSUXEI16_V: u16 = 0x0148;
    pub const OP_VSUXEI32_V: u16 = 0x0149;
    pub const OP_VSUXEI64_V: u16 = 0x014a;
    pub const OP_VSUXEI128_V: u16 = 0x014b;
    pub const OP_VSUXEI256_V: u16 = 0x014c;
    pub const OP_VSUXEI512_V: u16 = 0x014d;
    pub const OP_VSUXEI1024_V: u16 = 0x014e;
    pub const OP_VSOXEI8_V: u16 = 0x014f;
    pub const OP_VSOXEI16_V: u16 = 0x0150;
    pub const OP_VSOXEI32_V: u16 = 0x0151;
    pub const OP_VSOXEI64_V: u16 = 0x0152;
    pub const OP_VSOXEI128_V: u16 = 0x0153;
    pub const OP_VSOXEI256_V: u16 = 0x0154;
    pub const OP_VSOXEI512_V: u16 = 0x0155;
    pub const OP_VSOXEI1024_V: u16 = 0x0156;
    pub const OP_VL1RE8_V: u16 = 0x0157;
    pub const OP_VL1RE16_V: u16 = 0x0158;
    pub const OP_VL1RE32_V: u16 = 0x0159;
    pub const OP_VL1RE64_V: u16 = 0x015a;
    pub const OP_VL2RE8_V: u16 = 0x015b;
    pub const OP_VL2RE16_V: u16 = 0x015c;
    pub const OP_VL2RE32_V: u16 = 0x015d;
    pub const OP_VL2RE64_V: u16 = 0x015e;
    pub const OP_VL4RE8_V: u16 = 0x015f;
    pub const OP_VL4RE16_V: u16 = 0x0160;
    pub const OP_VL4RE32_V: u16 = 0x0161;
    pub const OP_VL4RE64_V: u16 = 0x0162;
    pub const OP_VL8RE8_V: u16 = 0x0163;
    pub const OP_VL8RE16_V: u16 = 0x0164;
    pub const OP_VL8RE32_V: u16 = 0x0165;
    pub const OP_VL8RE64_V: u16 = 0x0166;
    pub const OP_VS1R_V: u16 = 0x0167;
    pub const OP_VS2R_V: u16 = 0x0168;
    pub const OP_VS4R_V: u16 = 0x0169;
    pub const OP_VS8R_V: u16 = 0x016a;
    pub const OP_VMACC_VV: u16 = 0x016b;
    pub const OP_VMACC_VX: u16 = 0x016c;
    pub const OP_VNMSAC_VV: u16 = 0x016d;
    pub const OP_VNMSAC_VX: u16 = 0x016e;
    pub const OP_VMADD_VV: u16 = 0x016f;
    pub const OP_VMADD_VX: u16 = 0x0170;
    pub const OP_VNMSUB_VV: u16 = 0x0171;
    pub const OP_VNMSUB_VX: u16 = 0x0172;
    pub const OP_VSSRL_VV: u16 = 0x0173;
    pub const OP_VSSRL_VX: u16 = 0x0174;
    pub const OP_VSSRL_VI: u16 = 0x0175;
    pub const OP_VSSRA_VV: u16 = 0x0176;
    pub const OP_VSSRA_VX: u16 = 0x0177;
    pub const OP_VSSRA_VI: u16 = 0x0178;
    pub const OP_VSMUL_VV: u16 = 0x0179;
    pub const OP_VSMUL_VX: u16 = 0x017a;
    pub const OP_VWMACCU_VV: u16 = 0x017b;
    pub const OP_VWMACCU_VX: u16 = 0x017c;
    pub const OP_VWMACC_VV: u16 = 0x017d;
    pub const OP_VWMACC_VX: u16 = 0x017e;
    pub const OP_VWMACCSU_VV: u16 = 0x017f;
    pub const OP_VWMACCSU_VX: u16 = 0x0180;
    pub const OP_VWMACCUS_VX: u16 = 0x0181;
    pub const OP_VMERGE_VVM: u16 = 0x0182;
    pub const OP_VMERGE_VXM: u16 = 0x0183;
    pub const OP_VMERGE_VIM: u16 = 0x0184;
    pub const OP_VNCLIPU_WV: u16 = 0x0185;
    pub const OP_VNCLIPU_WX: u16 = 0x0186;
    pub const OP_VNCLIPU_WI: u16 = 0x0187;
    pub const OP_VNCLIP_WV: u16 = 0x0188;
    pub const OP_VNCLIP_WX: u16 = 0x0189;
    pub const OP_VNCLIP_WI: u16 = 0x018a;
    pub const OP_VREDSUM_VS: u16 = 0x018b;
    pub const OP_VREDAND_VS: u16 = 0x018c;
    pub const OP_VREDOR_VS: u16 = 0x018d;
    pub const OP_VREDXOR_VS: u16 = 0x018e;
    pub const OP_VREDMINU_VS: u16 = 0x018f;
    pub const OP_VREDMIN_VS: u16 = 0x0190;
    pub const OP_VREDMAXU_VS: u16 = 0x0191;
    pub const OP_VREDMAX_VS: u16 = 0x0192;
    pub const OP_VWREDSUMU_VS: u16 = 0x0193;
    pub const OP_VWREDSUM_VS: u16 = 0x0194;
    pub const OP_VCPOP_M: u16 = 0x0195;
    pub const OP_VMSBF_M: u16 = 0x0196;
    pub const OP_VMSOF_M: u16 = 0x0197;
    pub const OP_VMSIF_M: u16 = 0x0198;
    pub const OP_VIOTA_M: u16 = 0x0199;
    pub const OP_VID_V: u16 = 0x019a;
    pub const OP_VMV_X_S: u16 = 0x019b;
    pub const OP_VMV_S_X: u16 = 0x019c;
    pub const OP_VCOMPRESS_VM: u16 = 0x019d;
    pub const OP_VSLIDE1UP_VX: u16 = 0x019e;
    pub const OP_VSLIDEUP_VX: u16 = 0x019f;
    pub const OP_VSLIDEUP_VI: u16 = 0x01a0;
    pub const OP_VSLIDE1DOWN_VX: u16 = 0x01a1;
    pub const OP_VSLIDEDOWN_VX: u16 = 0x01a2;
    pub const OP_VSLIDEDOWN_VI: u16 = 0x01a3;
    pub const OP_VRGATHER_VX: u16 = 0x01a4;
    pub const OP_VRGATHER_VV: u16 = 0x01a5;
    pub const OP_VRGATHEREI16_VV: u16 = 0x01a6;
    pub const OP_VRGATHER_VI: u16 = 0x01a7;
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
    RVZifencei(RVZifencei),
    RVZcsr(RVZcsr),
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
pub fn funct3(bit: u32) -> u32 {
    slice(bit, 12, 3, 0)
}
#[inline(always)]
pub fn funct7(bit: u32) -> u32 {
    slice(bit, 25, 7, 0)
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
    match bit_u32 & 0b_111_00000000000_11 {
        // == Quadrant 0
        0b_000_00000000000_00 => {
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
        0b_010_00000000000_00 => Some(rv32!(
            RV32I,
            LW,
            Rd(Reg::X(Xx::new(c_r(bit_u32, 2)))),
            Rs1(Reg::X(Xx::new(c_r(bit_u32, 7)))),
            Imm32::from(c_sw_uimmediate(bit_u32))
        )),
        0b_011_00000000000_00 => {
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
        0b_100_00000000000_00 => None,
        0b_110_00000000000_00 => Some(
            // C.SW
            rv32!(
                RV32I,
                SW,
                Rs1(Reg::X(Xx::new(c_r(bit_u32, 7)))),
                Rs2(Reg::X(Xx::new(c_r(bit_u32, 2)))),
                Imm32::from(c_sw_uimmediate(bit_u32))
            ),
        ),
        0b_111_00000000000_00 => {
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
        0b_000_00000000000_01 => {
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
        0b_001_00000000000_01 => {
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
        0b_010_00000000000_01 => {
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
        0b_011_00000000000_01 => {
            let imm = c_immediate(bit_u32) << 12;
            if imm != 0 {
                let rd = rd(bit_u32);
                if rd == 2 {
                    // C.ADDI16SP
                    Some(rv32!(
                        RV32I,
                        ADDI,
                        Rd(Reg::X(XX::new(2))),
                        Rs1(Reg::X(XX::new(2))),
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
        0b_100_00000000000_01 => {
            let rd = c_r(bit_u32, 7);
            match bit_u32 & 0b_1_11_000_11000_00 {
                // C.SRLI64
                0b_0_00_000_00000_00 if bit_u32 & 0b_111_00 == 0 => Some(Instr::NOP),
                // C.SRAI64
                0b_0_01_000_00000_00 if bit_u32 & 0b_111_00 == 0 => Some(Instr::NOP),
                // C.SUB
                0b_0_11_000_00000_00 => Some(rv32!(
                    RV32I,
                    SUB,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(rd))),
                    Rs2(Reg::X(Xx::new(c_r(bit_u32, 2))))
                )),
                // C.XOR
                0b_0_11_000_01000_00 => Some(rv32!(
                    RV32I,
                    XOR,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(rd))),
                    Rs2(Reg::X(Xx::new(c_r(bit_u32, 2))))
                )),
                // C.OR
                0b_0_11_000_10000_00 => Some(rv32!(
                    RV32I,
                    OR,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(rd))),
                    Rs2(Reg::X(Xx::new(c_r(bit_u32, 2))))
                )),
                // C.AND
                0b_0_11_000_11000_00 => Some(rv32!(
                    RV32I,
                    AND,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(rd))),
                    Rs2(Reg::X(Xx::new(c_r(bit_u32, 2))))
                )),
                // C.SUBW
                0b_1_11_000_00000_00 => Some(rv64!(
                    RV64I,
                    SUBW,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(rd))),
                    Rs2(Reg::X(Xx::new(c_r(bit_u32, 2))))
                )),
                // C.ADDW
                0b_1_11_000_01000_00 => Some(rv64!(
                    RV64I,
                    ADDW,
                    Rd(Reg::X(Xx::new(rd))),
                    Rs1(Reg::X(Xx::new(rd))),
                    Rs2(Reg::X(Xx::new(c_r(bit_u32, 2))))
                )),
                // Reserved
                0b_1_11_000_10000_00 => None,
                // Reserved
                0b_1_11_000_11000_00 => None,
                _ => {
                    let uimm = c_uimmediate(bit_u32);
                    match (bit_u32 & 0b_11_000_00000_00, uimm) {
                        // Invalid instruction
                        (0b_00_000_00000_00, 0) => None,
                        // C.SRLI
                        (0b_00_000_00000_00, uimm) => Some(rv64!(
                            RV64I,
                            SRLI,
                            Rd(Reg::X(Xx::new(rd))),
                            Rs1(Reg::X(Xx::new(rd))),
                            Shamt((((bit_u32 >> 7) & 0x20) | ((bit_u32 >> 2) & 0x1f)) as u8)
                        )),
                        // Invalid instruction
                        (0b_01_000_00000_00, 0) => None,
                        // C.SRAI
                        (0b_01_000_00000_00, uimm) => Some(
                            // Itype::new_u(insts::OP_SRAI, rd, rd, uimm & u32::from(R::SHIFT_MASK)).0,
                            rv64!(
                                RV64I,
                                SRAI,
                                Rd(Reg::X(Xx::new(rd))),
                                Rs1(Reg::X(Xx::new(rd))),
                                Shamt(
                                    ((bit_u32 >> 7) & 0x20) as u8 | ((bit_u32 >> 2) & 0x1f) as u8
                                )
                            ),
                        ),
                        // C.ANDI
                        (0b_10_000_00000_00, _) => Some(rv32!(
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
        0b_101_00000000000_01 => Some(rv32!(
            RV32I,
            JAL,
            Rd(Reg::X(Xx::new(0))),
            Imm32::from(c_j_immediate(bit_u32))
        )),
        // C.BEQZ
        0b_110_00000000000_01 => Some(rv32!(
            RV32I,
            BEQ,
            Rs1(Reg::X(Xx::new(c_r(bit_u32, 7)))),
            Rs2(Reg::X(Xx::new(0))),
            Imm32::from(c_b_immediate(bit_u32))
        )),
        // C.BNEZ
        0b_111_00000000000_01 => Some(rv32!(
            RV32I,
            BEQ,
            Rs1(Reg::X(Xx::new(c_r(bit_u32, 7)))),
            Rs2(Reg::X(Xx::new(0))),
            Imm32::from(c_b_immediate(bit_u32))
        )),
        // == Quadrant 2
        0b_000_00000000000_10 => {
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
        0b_010_00000000000_10 => {
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
        0b_011_00000000000_10 => {
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
        0b_100_00000000000_10 => {
            match bit_u32 & 0b_1_00000_00000_00 {
                0b_0_00000_00000_00 => {
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
                0b_1_00000_00000_00 => {
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
        0b_110_00000000000_10 => Some(
            // C.SWSP
            rv32!(
                RV32I,
                SW,
                Rs1(Reg::X(Xx::new(2))),
                Rs2(Reg::X(Xx::new(c_rs2(bit_u32)))),
                Imm32::from(c_swsp_uimmediate(bit_u32))
            ),
        ),
        0b_111_00000000000_10 => {
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
    .map_or(|x| Instruction { instr: x }, None)
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
    if encoding >= 0 && encoding <= 31 {
        Reg::X(Xx::new(encoding as u32))
    } else {
        panic!("Inaccessible register encoding: {:b}", encoding)
    }
}

// TODO: require a smarter impl to decide RVI or RVE
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
    ($ident1:ident,$ident2:ident) => Instr::RV64(RV64Instr::$ident1($ident1::$ident2))
    ($ident1:ident,$ident2:ident, $($t:expr),*) =>
        Instr::RV64(RV64Instr::$ident1($ident1::$ident2($( $t, )*)))
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

impl Instruction {
    fn parse(bit: &[u8]) -> Instruction {
        if let Some(instr) = try_from_compressed(bit) {
            instr
        } else {
            let bit_u32 = u32::from_le_bytes(bit.split_at(4).0.try_into().unwrap());

            let opcode = slice(bit_u32, 0, 7, 0) as u16;

            let instr = match opcode {
                0b_0110111 => Some(u!(rv32, RV32I, LUI, bit_u32, gp)),
                0b_0010111 => Some(u!(rv32, RV32I, AUIPC, bit_u32, gp)),
                0b_1101111 => Some(j!(rv32, RV32I, JAL, bit_u32, gp)),
                0b_1100111 => {
                    match funct3(bit_u32) {
                        // I-type jump instructions
                        0b_000 => Some(i!(rv32, RV32I, JALR, bit_u32, gp)),
                        _ => None,
                    }
                }
                0b_0000011 => {
                    // I-type load instructions
                    match funct3(bit_u32) {
                        0b_000 => Some(i!(rv32, RV32I, LB, bit_u32, gp)),
                        0b_001 => Some(i!(rv32, RV32I, LH, bit_u32, gp)),
                        0b_010 => Some(i!(rv32, RV32I, LW, bit_u32, gp)),
                        0b_100 => Some(i!(rv32, RV32I, LBU, bit_u32, gp)),
                        0b_101 => Some(i!(rv32, RV32I, LHU, bit_u32, gp)),
                        0b_110 => Some(i!(rv64, RV64I, LWU, bit_u32, gp)),
                        0b_011 => Some(i!(rv64, RV64I, LD, bit_u32, gp)),
                        _ => None,
                    }
                }
                0b_0010011 => {
                    let funct3_val = funct3(bit_u32);

                    match funct3_val {
                        // I-type ALU instructions
                        0b_000 => Some(i!(rv32, RV32I, ADDI, bit_u32, gp)),
                        0b_010 => Some(i!(rv32, RV32I, SLTI, bit_u32, gp)),
                        0b_011 => Some(i!(rv32, RV32I, SLTIU, bit_u32, gp)),
                        0b_100 => Some(i!(rv32, RV32I, XORI, bit_u32, gp)),
                        0b_110 => Some(i!(rv32, RV32I, ORI, bit_u32, gp)),
                        0b_111 => Some(i!(rv32, RV32I, ANDI, bit_u32, gp)),
                        // I-type special ALU instructions
                        0b_001 | 0b_101 => {
                            let top6_val = funct7(bit_u32) >> 1;
                            match (funct3_val, top6_val) {
                                (0b_001, 0b_000000) => {
                                    Some(r_shamt!(rv64, RV64I, SLLI, bit_u32, gp))
                                }
                                (0b_101, 0b_000000) => {
                                    Some(r_shamt!(rv64, RV64I, SRLI, bit_u32, gp))
                                }
                                (0b_101, 0b_010000) => {
                                    Some(r_shamt!(rv64, RV64I, SRAI, bit_u32, gp))
                                }
                                _ => None,
                            }
                        }
                        0b_1100011 => match funct3(bit_u32) {
                            0b_000 => Some(b!(rv32, RV32I, BEQ, bit_u32, gp)),
                            0b_001 => Some(b!(rv32, RV32I, BNE, bit_u32, gp)),
                            0b_100 => Some(b!(rv32, RV32I, BLT, bit_u32, gp)),
                            0b_101 => Some(b!(rv32, RV32I, BGE, bit_u32, gp)),
                            0b_110 => Some(b!(rv32, RV32I, BLTU, bit_u32, gp)),
                            0b_111 => Some(b!(rv32, RV32I, BGEU, bit_u32, gp)),
                            _ => None,
                        },
                        0b_0100011 => match funct3(bit_u32) {
                            0b_000 => Some(s!(rv32, RV32I, SB, bit_u32, gp)),
                            0b_001 => Some(s!(rv32, RV32I, SH, bit_u32, gp)),
                            0b_010 => Some(s!(rv32, RV32I, SW, bit_u32, gp)),
                            0b_011 => Some(s!(rv32, RV32I, SD, bit_u32, gp)),
                            _ => None,
                        },
                        0b_0110011 => match (funct3(bit_u32), funct7(bit_u32)) {
                            (0b_000, 0b_0000000) => Some(r!(rv32, RV32I, ADD, bit_u32, gp)),
                            (0b_000, 0b_0100000) => Some(r!(rv32, RV32I, SUB, bit_u32, gp)),
                            (0b_001, 0b_0000000) => Some(r!(rv32, RV32I, SLL, bit_u32, gp)),
                            (0b_010, 0b_0000000) => Some(r!(rv32, RV32I, SLT, bit_u32, gp)),
                            (0b_011, 0b_0000000) => Some(r!(rv32, RV32I, SLTU, bit_u32, gp)),
                            (0b_100, 0b_0000000) => Some(r!(rv32, RV32I, XOR, bit_u32, gp)),
                            (0b_101, 0b_0000000) => Some(r!(rv32, RV32I, SRL, bit_u32, gp)),
                            (0b_101, 0b_0100000) => Some(r!(rv32, RV32I, SRA, bit_u32, gp)),
                            (0b_110, 0b_0000000) => Some(r!(rv32, RV32I, OR, bit_u32, gp)),
                            (0b_111, 0b_0000000) => Some(r!(rv32, RV32I, AND, bit_u32, gp)),
                            _ => None,
                        },
                        0b_0001111 => {
                            const FENCE_TSO: u32 = 0b1000_0011_0011_00000_000_00000_0001111;
                            const FENCE_PAUSE: u32 = 0b0000_0001_0000_00000_000_00000_0001111;
                            const FENCE_I: u32 = 0b_0000_0000_0000_00000_001_00000_0001111;
                            match funct3(bit_u32) {
                                0b000 => match bit_u32 {
                                    FENCE_TSO => Some(rv32!(RV32I, FENCE_TSO)),
                                    FENCE_PAUSE => Some(rv32!(RV32I, PAUSE)),
                                    _fence => fence!(rv32, RV32I, FENCE, bit_u32, gp),
                                },
                                0b001 => Some(i!(rv64_no_e, RVZifencei, FENCE_I, bit_u32, gp)),
                                _ => None,
                            }
                        }
                        0b_1110011 => match bit_u32 {
                            0b_000000000000_00000_000_00000_1110011 => Some(rv32!(RV32I, ECALL)),
                            0b_000000000001_00000_000_00000_1110011 => Some(rv32!(RV32I, EBREAK)),
                            _ => None,
                        },
                        0b_0011011 => {
                            let funct3_value = funct3(bit_u32);
                            match funct3_value {
                                0b_000 => Some(i!(rv64, RV64I, ADDIW, bit_u32, gp)),
                                0b_001 | 0b_101 => {
                                    let funct7_value = funct7(bit_u32);
                                    match (funct3_value, funct7_value) {
                                        (0b_001, 0b_0000000) => {
                                            Some(r_shamt!(rv64, RV64I, SLLIW, bit_u32, gp))
                                        }
                                        (0b_101, 0b_0000000) => {
                                            Some(r_shamt!(rv64, RV64I, SRLIW, bit_u32, gp))
                                        }
                                        (0b_101, 0b_0100000) => {
                                            Some(r_shamt!(rv64, RV64I, SRAIW, bit_u32, gp))
                                        }
                                        _ => None,
                                    }
                                }
                                _ => None,
                            }
                        }
                        0b_0111011 => {
                            let inst_opt = match (funct3(bit_u32), funct7(bit_u32)) {
                                (0b_000, 0b_0000000) => Some(insts::OP_ADDW),
                                (0b_000, 0b_0100000) => Some(insts::OP_SUBW),
                                (0b_001, 0b_0000000) => Some(insts::OP_SLLW),
                                (0b_101, 0b_0000000) => Some(insts::OP_SRLW),
                                (0b_101, 0b_0100000) => Some(insts::OP_SRAW),
                                _ => None,
                            };
                            inst_opt.map(|inst| {
                                Rtype::new(inst, rd(bit_u32), rs1(bit_u32), rs2(bit_u32)).0
                            })
                        }
                        _ => None,
                    }
                }
                0b_0111011 => {
                    let funct3_value = funct3(bit_u32);
                    let funct7_value = funct7(bit_u32);
                    let inst_opt = match (funct3_value, funct7_value) {
                        (0b_000, 0b_0000100) => Some(insts::OP_ADDUW),
                        (0b_001, 0b_0110000) => Some(insts::OP_ROLW),
                        (0b_010, 0b_0010000) => Some(insts::OP_SH1ADDUW),
                        (0b_100, 0b_0000100) => {
                            if unsafe { BIT_LENGTH == 1 } && rs2(bit_u32) == 0 {
                                Some(insts::OP_ZEXTH)
                            } else {
                                None
                            }
                        }
                        (0b_100, 0b_0010000) => Some(insts::OP_SH2ADDUW),
                        (0b_101, 0b_0110000) => Some(insts::OP_RORW),
                        (0b_110, 0b_0010000) => Some(insts::OP_SH3ADDUW),
                        _ => None,
                    };
                    inst_opt.map(|inst| Rtype::new(inst, rd(bit_u32), rs1(bit_u32), rs2(bit_u32)).0)
                }
                0b_0110011 => {
                    let funct3_value = funct3(bit_u32);
                    let funct7_value = funct7(bit_u32);
                    let inst_opt = match (funct3_value, funct7_value) {
                        (0b_111, 0b_0100000) => Some(insts::OP_ANDN),
                        (0b_110, 0b_0100000) => Some(insts::OP_ORN),
                        (0b_100, 0b_0100000) => Some(insts::OP_XNOR),
                        (0b_001, 0b_0110000) => Some(insts::OP_ROL),
                        (0b_101, 0b_0110000) => Some(insts::OP_ROR),
                        (0b_001, 0b_0110100) => Some(insts::OP_BINV),
                        (0b_001, 0b_0010100) => Some(insts::OP_BSET),
                        (0b_001, 0b_0100100) => Some(insts::OP_BCLR),
                        (0b_101, 0b_0100100) => Some(insts::OP_BEXT),
                        (0b_010, 0b_0010000) => Some(insts::OP_SH1ADD),
                        (0b_100, 0b_0010000) => Some(insts::OP_SH2ADD),
                        (0b_110, 0b_0010000) => Some(insts::OP_SH3ADD),
                        (0b_001, 0b_0000101) => Some(insts::OP_CLMUL),
                        (0b_011, 0b_0000101) => Some(insts::OP_CLMULH),
                        (0b_010, 0b_0000101) => Some(insts::OP_CLMULR),
                        (0b_100, 0b_0000101) => Some(insts::OP_MIN),
                        (0b_101, 0b_0000101) => Some(insts::OP_MINU),
                        (0b_110, 0b_0000101) => Some(insts::OP_MAX),
                        (0b_111, 0b_0000101) => Some(insts::OP_MAXU),
                        _ => None,
                    };
                    inst_opt.map(|inst| Rtype::new(inst, rd(bit_u32), rs1(bit_u32), rs2(bit_u32)).0)
                }
                0b_0010011 => {
                    let funct3_value = funct3(bit_u32);
                    let funct7_value = funct7(bit_u32);
                    let rs2_value = rs2(bit_u32);
                    let inst_opt = match (funct7_value, funct3_value, rs2_value) {
                        (0b_0010100, 0b_101, 0b_00111) => Some(insts::OP_ORCB),
                        (0b_0110101, 0b_101, 0b_11000) => Some(insts::OP_REV8),
                        (0b_0110000, 0b_001, 0b_00000) => Some(insts::OP_CLZ),
                        (0b_0110000, 0b_001, 0b_00010) => Some(insts::OP_CPOP),
                        (0b_0110000, 0b_001, 0b_00001) => Some(insts::OP_CTZ),
                        (0b_0110000, 0b_001, 0b_00100) => Some(insts::OP_SEXTB),
                        (0b_0110000, 0b_001, 0b_00101) => Some(insts::OP_SEXTH),
                        _ => None,
                    };
                    if let Some(inst) = inst_opt {
                        Some(Rtype::new(inst, rd(bit_u32), rs1(bit_u32), rs2(bit_u32)).0)
                    } else {
                        let inst_opt = match (funct7_value >> 1, funct3_value) {
                            (0b_010010, 0b_001) => Some(insts::OP_BCLRI),
                            (0b_010010, 0b_101) => Some(insts::OP_BEXTI),
                            (0b_011010, 0b_001) => Some(insts::OP_BINVI),
                            (0b_001010, 0b_001) => Some(insts::OP_BSETI),
                            (0b_011000, 0b_101) => Some(insts::OP_RORI),
                            _ => None,
                        };
                        inst_opt.map(|inst| {
                            Itype::new_u(inst, rd(bit_u32), rs1(bit_u32), slice(bit_u32, 20, 6, 0))
                                .0
                        })
                    }
                }
                0b_0011011 => {
                    let funct3_value = funct3(bit_u32);
                    let funct7_value = funct7(bit_u32);
                    let rs2_value = rs2(bit_u32);

                    match funct7_value {
                        0b_0110000 => match funct3_value {
                            0b_001 => {
                                let inst_opt = match rs2_value {
                                    0b_00000 => Some(insts::OP_CLZW),
                                    0b_00010 => Some(insts::OP_CPOPW),
                                    0b_00001 => Some(insts::OP_CTZW),
                                    _ => None,
                                };
                                inst_opt.map(|inst| {
                                    Rtype::new(inst, rd(bit_u32), rs1(bit_u32), rs2_value).0
                                })
                            }
                            0b_101 => Some(
                                Itype::new_u(
                                    insts::OP_RORIW,
                                    rd(bit_u32),
                                    rs1(bit_u32),
                                    slice(bit_u32, 20, 5, 0),
                                )
                                .0,
                            ),
                            _ => None,
                        },
                        _ => {
                            if funct7_value >> 1 == 0b_000010 && funct3_value == 0b_001 {
                                Some(
                                    Itype::new_u(
                                        insts::OP_SLLIUW,
                                        rd(bit_u32),
                                        rs1(bit_u32),
                                        slice(bit_u32, 20, 6, 0),
                                    )
                                    .0,
                                )
                            } else {
                                None
                            }
                        }
                    }
                }
                0b_0110011 => match funct3(bit_u32) {
                    0b_000 => Some(insts::OP_MUL),
                    0b_001 => Some(insts::OP_MULH),
                    0b_010 => Some(insts::OP_MULHSU),
                    0b_011 => Some(insts::OP_MULHU),
                    0b_100 => Some(insts::OP_DIV),
                    0b_101 => Some(insts::OP_DIVU),
                    0b_110 => Some(insts::OP_REM),
                    0b_111 => Some(insts::OP_REMU),
                    _ => None,
                },
                0b_0111011 => match funct3(bit_u32) {
                    0b_000 => Some(insts::OP_MULW),
                    0b_100 => Some(insts::OP_DIVW),
                    0b_101 => Some(insts::OP_DIVUW),
                    0b_110 => Some(insts::OP_REMW),
                    0b_111 => Some(insts::OP_REMUW),
                    _ => None,
                },

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
        match instruction.instr {
            Instr::RV32(_) => unsafe { assert_eq!(BIT_LENGTH, 0) },
            Instr::RV64(_) => unsafe { assert_eq!(BIT_LENGTH, 1) },
            Instr::RV128(_) => unsafe { assert_eq!(BIT_LENGTH, 2) },
            Instr::NOP => {}
        }
        Some((address, instruction))
    }
}

#[cfg(test)]
mod tests {
    use crate::frontend::instruction::*;

    #[test]
    fn test_rvi_addi() {
        // jal x0, -6*4
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
    fn test_rvi() {
        // jal x0, -6*4
        let instr_asm: u32 = 0b11011000010111;
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
}
