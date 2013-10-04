/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/x64/x64_emit.h>

#include <xenia/cpu/cpu-private.h>


using namespace xe::cpu;
using namespace xe::cpu::ppc;

using namespace AsmJit;


namespace xe {
namespace cpu {
namespace x64 {


// Most of this file comes from:
// http://biallas.net/doc/vmx128/vmx128.txt
// https://github.com/kakaroto/ps3ida/blob/master/plugins/PPCAltivec/src/main.cpp


#define OP(x)             ((((uint32_t)(x)) & 0x3f) << 26)
#define VX128(op, xop)    (OP(op) | (((uint32_t)(xop)) & 0x3d0))
#define VX128_1(op, xop)  (OP(op) | (((uint32_t)(xop)) & 0x7f3))
#define VX128_2(op, xop)  (OP(op) | (((uint32_t)(xop)) & 0x210))
#define VX128_3(op, xop)  (OP(op) | (((uint32_t)(xop)) & 0x7f0))
#define VX128_4(op, xop)  (OP(op) | (((uint32_t)(xop)) & 0x730))
#define VX128_5(op, xop)  (OP(op) | (((uint32_t)(xop)) & 0x10))
#define VX128_P(op, xop)  (OP(op) | (((uint32_t)(xop)) & 0x630))

#define VX128_VD128 (i.VX128.VD128l | (i.VX128.VD128h << 5))
#define VX128_VA128 (i.VX128.VA128l | (i.VX128.VA128h << 5) | (i.VX128.VA128H << 6))
#define VX128_VB128 (i.VX128.VB128l | (i.VX128.VB128h << 5))
#define VX128_1_VD128 (i.VX128_1.VD128l | (i.VX128_1.VD128h << 5))
#define VX128_2_VD128 (i.VX128_2.VD128l | (i.VX128_2.VD128h << 5))
#define VX128_2_VA128 (i.VX128_2.VA128l | (i.VX128_2.VA128h << 5) | (i.VX128_2.VA128H << 6))
#define VX128_2_VB128 (i.VX128_2.VB128l | (i.VX128_2.VD128h << 5))
#define VX128_2_VC    (i.VX128_2.VC)
#define VX128_3_VD128 (i.VX128_3.VD128l | (i.VX128_3.VD128h << 5))
#define VX128_3_VB128 (i.VX128_3.VB128l | (i.VX128_3.VB128h << 5))
#define VX128_3_IMM   (i.VX128_3.IMM)
#define VX128_R_VD128 (i.VX128_R.VD128l | (i.VX128_R.VD128h << 5))
#define VX128_R_VA128 (i.VX128_R.VA128l | (i.VX128_R.VA128h << 5) | (i.VX128_R.VA128H << 6))
#define VX128_R_VB128 (i.VX128_R.VB128l | (i.VX128_R.VB128h << 5))


XEEMITTER(dst,            0x7C0002AC, XDSS)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(dstst,          0x7C0002EC, XDSS)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(dss,            0x7C00066C, XDSS)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lvebx,          0x7C00000E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lvehx,          0x7C00004E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_lvewx_(X64Emitter& e, X86Compiler& c, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(lvewx,          0x7C00008E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvewx_(e, c, i, i.X.RT, i.X.RA, i.X.RB);
}
XEEMITTER(lvewx128,       VX128_1(4, 131),  VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvewx_(e, c, i, i.X.RT, i.X.RA, i.X.RB);
}

int InstrEmit_lvsl_(X64Emitter& e, X86Compiler& c, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(lvsl,           0x7C00000C, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvsl_(e, c, i, i.X.RT, i.X.RA, i.X.RB);
}
XEEMITTER(lvsl128,        VX128_1(4, 3),    VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvsl_(e, c, i, i.X.RT, i.X.RA, i.X.RB);
}

int InstrEmit_lvsr_(X64Emitter& e, X86Compiler& c, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(lvsr,           0x7C00004C, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvsr_(e, c, i, i.X.RT, i.X.RA, i.X.RB);
}
XEEMITTER(lvsr128,        VX128_1(4, 67),   VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvsr_(e, c, i, i.X.RT, i.X.RA, i.X.RB);
}

int InstrEmit_lvx_(X64Emitter& e, X86Compiler& c, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
  GpVar ea(c.newGpVar());
  c.mov(ea, e.gpr_value(rb));
  if (ra) {
    c.add(ea, e.gpr_value(ra));
  }
  XmmVar v = e.ReadMemoryXmm(i.address, ea, 4);
  c.shufps(v, v, imm(0x1B));
  e.update_vr_value(vd, v);
  e.TraceVR(vd);
  return 0;
}
XEEMITTER(lvx,            0x7C0000CE, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvx_(e, c, i, i.X.RT, i.X.RA, i.X.RB);
}
XEEMITTER(lvx128,         VX128_1(4, 195),  VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvx_(e, c, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}
XEEMITTER(lvxl,           0x7C0002CE, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvx(e, c, i);
}
XEEMITTER(lvxl128,        VX128_1(4, 707),  VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvx128(e, c, i);
}

XEEMITTER(stvebx,         0x7C00010E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(stvehx,         0x7C00014E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_stvewx_(X64Emitter& e, X86Compiler& c, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(stvewx,         0x7C00018E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvewx_(e, c, i, i.X.RT, i.X.RA, i.X.RB);
}
XEEMITTER(stvewx128,      VX128_1(4, 387),  VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_stvx_(X64Emitter& e, X86Compiler& c, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
  GpVar ea(c.newGpVar());
  c.mov(ea, e.gpr_value(rb));
  if (ra) {
    c.add(ea, e.gpr_value(ra));
  }
  XmmVar v = e.vr_value(vd);
  c.shufps(v, v, imm(0x1B));
  e.WriteMemoryXmm(i.address, ea, 4, v);
  e.TraceVR(vd);
  return 0;
}
XEEMITTER(stvx,           0x7C0001CE, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvx_(e, c, i, i.X.RT, i.X.RA, i.X.RB);
}
XEEMITTER(stvx128,        VX128_1(4, 451),  VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvx_(e, c, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}
XEEMITTER(stvxl,          0x7C0003CE, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvx(e, c, i);
}
XEEMITTER(stvxl128,       VX128_1(4, 963),  VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvx128(e, c, i);
}

int InstrEmit_lvlx_(X64Emitter& e, X86Compiler& c, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(lvlx,           0x7C00040E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvlx_(e, c, i, i.X.RT, i.X.RA, i.X.RB);
}
XEEMITTER(lvlx128,        VX128_1(4, 1027), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvlx_(e, c, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}
XEEMITTER(lvlxl,          0x7C00060E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvlx(e, c, i);
}
XEEMITTER(lvlxl128,       VX128_1(4, 1539), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvlx128(e, c, i);
}

int InstrEmit_lvrx_(X64Emitter& e, X86Compiler& c, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(lvrx,           0x7C00044E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvrx_(e, c, i, i.X.RT, i.X.RA, i.X.RB);
}
XEEMITTER(lvrx128,        VX128_1(4, 1091), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvrx_(e, c, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}
XEEMITTER(lvrxl,          0x7C00064E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvrx(e, c, i);
}
XEEMITTER(lvrxl128,       VX128_1(4, 1603), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvrx128(e, c, i);
}

int InstrEmit_stvlx_(X64Emitter& e, X86Compiler& c, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(stvlx,          0x7C00050E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvlx_(e, c, i, i.X.RT, i.X.RA, i.X.RB);
}
XEEMITTER(stvlx128,       VX128_1(4, 1283), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvlx_(e, c, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}
XEEMITTER(stvlxl,         0x7C00070E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvlx(e, c, i);
}
XEEMITTER(stvlxl128,      VX128_1(4, 1795), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvlx128(e, c, i);
}

int InstrEmit_stvrx_(X64Emitter& e, X86Compiler& c, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(stvrx,          0x7C00054E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvrx_(e, c, i, i.X.RT, i.X.RA, i.X.RB);
}
XEEMITTER(stvrx128,       VX128_1(4, 1347), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvrx_(e, c, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}
XEEMITTER(stvrxl,         0x7C00074E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvrx(e, c, i);
}
XEEMITTER(stvrxl128,      VX128_1(4, 1859), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvrx128(e, c, i);
}

XEEMITTER(mfvscr,         0x10000604, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtvscr,         0x10000644, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vaddcuw,        0x10000180, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vaddfp,         0x1000000A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vaddfp128,      VX128(5, 16),     VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vaddsbs,        0x10000300, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vaddshs,        0x10000340, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vaddsws,        0x10000380, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vaddubm,        0x10000000, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vaddubs,        0x10000200, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vadduhm,        0x10000040, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vadduhs,        0x10000240, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vadduwm,        0x10000080, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vadduws,        0x10000280, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vand_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t va, uint32_t vb) {
  // VD <- (VA) & (VB)
  XmmVar v(c.newXmmVar());
  c.movaps(v, e.vr_value(vb));
  c.pand(v, e.vr_value(va));
  e.update_vr_value(vd, v);
  e.TraceVR(vd, va, vb);
  return 0;
}
XEEMITTER(vand,           0x10000404, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vand_(e, c, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vand128,        VX128(5, 528),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vand_(e, c, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vandc_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t va, uint32_t vb) {
  // VD <- (VA) & ¬(VB)
  XmmVar v(c.newXmmVar());
  c.movaps(v, e.vr_value(vb));
  c.pandn(v, e.vr_value(va));
  e.update_vr_value(vd, v);
  e.TraceVR(vd, va, vb);
  return 0;
}
XEEMITTER(vandc,          0x10000444, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vandc_(e, c, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vandc128,       VX128(5, 592),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vandc_(e, c, VX128_VD128, VX128_VA128, VX128_VB128);
}

XEEMITTER(vavgsb,         0x10000502, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vavgsh,         0x10000542, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vavgsw,         0x10000582, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vavgub,         0x10000402, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vavguh,         0x10000442, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vavguw,         0x10000482, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcfsx,          0x1000034A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcsxwfp128,     VX128_3(6, 688),  VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // (VD) <- float(VB) / 2^uimm
  XmmVar v(c.newXmmVar());
  // TODO(benvanik): verify this is right - values may be out of range.
  c.cvtdq2ps(v, e.vr_value(VX128_3_VB128));
  uint32_t uimm = VX128_3_IMM;
  uimm = uimm ? (2 << (uimm - 1)) : 1;
  // TODO(benvanik): this could likely be made much faster.
  GpVar vt(c.newGpVar());
  c.mov(vt, imm(uimm));
  XmmVar vt_xmm(c.newXmmVar());
  c.movd(vt_xmm, vt.r32());
  c.cvtdq2ps(vt_xmm, vt_xmm);
  c.shufps(vt_xmm, vt_xmm, imm(0));
  c.divps(v, vt_xmm);
  e.update_vr_value(VX128_3_VD128, v);
  e.TraceVR(VX128_3_VD128, VX128_3_VB128);
  return 0;
}

XEEMITTER(vcfpsxws128,    VX128_3(6, 560),  VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcfux,          0x1000030A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcuxwfp128,     VX128_3(6, 752),  VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcfpuxws128,    VX128_3(6, 624),  VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vcmpbfp_(X64Emitter& e, X86Compiler& c, InstrData& i, uint32_t vd, uint32_t va, uint32_t vb, uint32_t rc) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vcmpbfp,        0x100003C6, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpbfp_(e, c, i, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpbfp128,     VX128(6, 384),    VX128_R)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpbfp_(e, c, i, VX128_R_VD128, VX128_R_VA128, VX128_R_VB128, i.VX128_R.Rc);
}

void InstrEmit_vcmp_cr6_(X64Emitter& e, X86Compiler& c, XmmVar& v) {
  // Testing for all 1's and all 0's.
  // if (Rc) CR6 = all_equal | 0 | none_equal | 0

  GpVar gt(c.newGpVar());
  GpVar cr(c.newGpVar());
  c.xor_(cr, cr);

  // We do this fast by extracting the high bits (as all bits are the same)
  // and testing those.
  GpVar bmask(c.newGpVar());
  c.pmovmskb(bmask, v);

  // zero = none_equal
  c.test(bmask, bmask);
  c.mov(gt, imm(0x2)); // none_equal=0b0010
  c.cmovz(cr, gt);

  // !zero = all_equal
  c.not_(bmask);
  c.test(bmask, bmask);
  c.mov(gt, imm(0x8)); // all_equal=0b1000
  c.cmovz(cr, gt);

  e.update_cr_value(6, cr);
}

// http://x86.renejeschke.de/html/file_module_x86_id_37.html
// These line up to the cmpps ops, except at the end where we have our own
// emulated ops for gt/etc that don't exist in the instruction set.
enum vcmpxxfp_op {
  vcmpxxfp_eq     = 0,
  vcmpxxfp_lt     = 1,
  vcmpxxfp_le     = 2,
  vcmpxxfp_unord  = 3,
  vcmpxxfp_neq    = 4,
  vcmpxxfp_nlt    = 5,
  vcmpxxfp_nle    = 6,
  vcmpxxfp_ord    = 7,
  // Emulated ops:
  vcmpxxfp_gt     = 8,
  vcmpxxfp_ge     = 9,
};
int InstrEmit_vcmpxxfp_(X64Emitter& e, X86Compiler& c, InstrData& i, vcmpxxfp_op cmpop, uint32_t vd, uint32_t va, uint32_t vb, uint32_t rc) {
  // (VD.xyzw) = (VA.xyzw) OP (VB.xyzw) ? 0xFFFFFFFF : 0x00000000
  // if (Rc) CR6 = all_equal | 0 | none_equal | 0
  // If an element in either VA or VB is NaN the result will be 0x00000000
  XmmVar v(c.newXmmVar());
  switch (cmpop) {
  // Supported ops:
  default:
    c.movaps(v, e.vr_value(va));
    c.cmpps(v, e.vr_value(vb), imm(cmpop));
    break;
  // Emulated ops:
  case vcmpxxfp_gt:
    c.movaps(v, e.vr_value(vb));
    c.cmpps(v, e.vr_value(va), imm(vcmpxxfp_lt));
    break;
  case vcmpxxfp_ge:
    c.movaps(v, e.vr_value(vb));
    c.cmpps(v, e.vr_value(va), imm(vcmpxxfp_le));
    break;
  }
  e.update_vr_value(vd, v);
  if (rc) {
    InstrEmit_vcmp_cr6_(e, c, v);
  }
  e.TraceVR(vd, va, vb);
  return 0;
}

XEEMITTER(vcmpeqfp,       0x100000C6, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxfp_(e, c, i, vcmpxxfp_eq, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpeqfp128,    VX128(6, 0),      VX128_R)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxfp_(e, c, i, vcmpxxfp_eq, VX128_R_VD128, VX128_R_VA128, VX128_R_VB128, i.VX128_R.Rc);
}
XEEMITTER(vcmpgefp,       0x100001C6, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxfp_(e, c, i, vcmpxxfp_ge, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpgefp128,    VX128(6, 128),    VX128_R)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxfp_(e, c, i, vcmpxxfp_ge, VX128_R_VD128, VX128_R_VA128, VX128_R_VB128, i.VX128_R.Rc);
}
XEEMITTER(vcmpgtfp,       0x100002C6, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxfp_(e, c, i, vcmpxxfp_gt, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpgtfp128,    VX128(6, 256),    VX128_R)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxfp_(e, c, i, vcmpxxfp_gt, VX128_R_VD128, VX128_R_VA128, VX128_R_VB128, i.VX128_R.Rc);
}

enum vcmpxxi_op {
  vcmpxxi_eq          = 0,
  vcmpxxi_gt_signed   = 1,
  vcmpxxi_gt_unsigned = 2,
};
int InstrEmit_vcmpxxi_(X64Emitter& e, X86Compiler& c, InstrData& i, vcmpxxi_op cmpop, uint32_t width, uint32_t vd, uint32_t va, uint32_t vb, uint32_t rc) {
  // (VD.xyzw) = (VA.xyzw) OP (VB.xyzw) ? 0xFFFFFFFF : 0x00000000
  // if (Rc) CR6 = all_equal | 0 | none_equal | 0
  // If an element in either VA or VB is NaN the result will be 0x00000000
  XmmVar v(c.newXmmVar());
  c.movaps(v, e.vr_value(va));
  switch (cmpop) {
  case vcmpxxi_eq:
    switch (width) {
    case 1:
      c.pcmpeqb(v, e.vr_value(vb));
      break;
    case 2:
      c.pcmpeqw(v, e.vr_value(vb));
      break;
    case 4:
      c.pcmpeqd(v, e.vr_value(vb));
      break;
    default: XEASSERTALWAYS(); return 1;
    }
    break;
  case vcmpxxi_gt_signed:
    switch (width) {
    case 1:
      c.pcmpgtb(v, e.vr_value(vb));
      break;
    case 2:
      c.pcmpgtw(v, e.vr_value(vb));
      break;
    case 4:
      c.pcmpgtd(v, e.vr_value(vb));
      break;
    default: XEASSERTALWAYS(); return 1;
    }
    break;
  case vcmpxxi_gt_unsigned:
    // Nasty, as there is no unsigned variant.
    c.int3();
    XEINSTRNOTIMPLEMENTED();
    return 1;
  default: XEASSERTALWAYS(); return 1;
  }
  e.update_vr_value(vd, v);
  if (rc) {
    InstrEmit_vcmp_cr6_(e, c, v);
  }
  e.TraceVR(vd, va, vb);
  return 0;
}
XEEMITTER(vcmpequb,       0x10000006, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxi_(e, c, i, vcmpxxi_eq, 1, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpequh,       0x10000046, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxi_(e, c, i, vcmpxxi_eq, 2, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpequw,       0x10000086, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxi_(e, c, i, vcmpxxi_eq, 4, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpequw128,    VX128(6, 512),    VX128_R)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxi_(e, c, i, vcmpxxi_eq, 4, VX128_R_VD128, VX128_R_VA128, VX128_R_VB128, i.VX128_R.Rc);
}
XEEMITTER(vcmpgtsb,       0x10000306, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxi_(e, c, i, vcmpxxi_gt_signed, 1, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpgtsh,       0x10000346, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxi_(e, c, i, vcmpxxi_gt_signed, 2, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpgtsw,       0x10000386, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxi_(e, c, i, vcmpxxi_gt_signed, 4, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpgtub,       0x10000206, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxi_(e, c, i, vcmpxxi_gt_unsigned, 1, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpgtuh,       0x10000246, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxi_(e, c, i, vcmpxxi_gt_unsigned, 2, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpgtuw,       0x10000286, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vcmpxxi_(e, c, i, vcmpxxi_gt_unsigned, 4, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}

XEEMITTER(vctsxs,         0x100003CA, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vctuxs,         0x1000038A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vexptefp,       0x1000018A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vexptefp128,    VX128_3(6, 1712), VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vlogefp,        0x100001CA, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vlogefp128,     VX128_3(6, 1776), VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmaddfp_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc) {
  // (VD) <- ((VA) * (VC)) + (VB)
  // TODO(benvanik): use AVX, which has a fused multiply-add
  XmmVar v(c.newXmmVar());
  c.movaps(v, e.vr_value(va));
  c.mulps(v, e.vr_value(vc));
  c.addps(v, e.vr_value(vb));
  e.update_vr_value(vd, v);
  e.TraceVR(vd, va, vb, vc);
  return 0;
}
XEEMITTER(vmaddfp,        0x1000002E, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // (VD) <- ((VA) * (VC)) + (VB)
  return InstrEmit_vmaddfp_(e, c, i.VXA.VD, i.VXA.VA, i.VXA.VB, i.VXA.VC);
}
XEEMITTER(vmaddfp128,     VX128(5, 208),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // (VD) <- ((VA) * (VB)) + (VD)
  // NOTE: this resuses VD and swaps the arg order!
  return InstrEmit_vmaddfp_(e, c, VX128_VD128, VX128_VA128, VX128_VD128, VX128_VB128);
}

XEEMITTER(vmaddcfp128,    VX128(5, 272),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // (VD) <- ((VA) * (VD)) + (VB)
  XmmVar v(c.newXmmVar());
  c.movaps(v, e.vr_value(VX128_VA128));
  c.mulps(v, e.vr_value(VX128_VD128));
  c.addps(v, e.vr_value(VX128_VB128));
  e.update_vr_value(VX128_VD128, v);
  e.TraceVR(VX128_VD128, VX128_VA128, VX128_VB128);
  return 0;
}

XEEMITTER(vmaxfp,         0x1000040A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vmaxfp128,      VX128(6, 640),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmaxsb,         0x10000102, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmaxsh,         0x10000142, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmaxsw,         0x10000182, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmaxub,         0x10000002, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmaxuh,         0x10000042, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmaxuw,         0x10000082, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmhaddshs,      0x10000020, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmhraddshs,     0x10000021, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vminfp,         0x1000044A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vminfp128,      VX128(6, 704),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vminsb,         0x10000302, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vminsh,         0x10000342, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vminsw,         0x10000382, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vminub,         0x10000202, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vminuh,         0x10000242, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vminuw,         0x10000282, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmladduhm,      0x10000022, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmrghb,         0x1000000C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmrghh,         0x1000004C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmrghw_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD.x) = (VA.x)
  // (VD.y) = (VB.x)
  // (VD.z) = (VA.y)
  // (VD.w) = (VB.y)
  if (e.cpu_feature_mask() & kX86FeatureSse41) {
    // | VA.x | VA.x | VA.y | VA.y |
    XmmVar v(c.newXmmVar());
    c.movaps(v, e.vr_value(va));
    c.shufps(v, v, imm(0x50));
    // | VB.x | VB.x | VB.y | VB.y |
    XmmVar vt(c.newXmmVar());
    c.movaps(vt, e.vr_value(vb));
    c.shufps(vt, vt, imm(0x50));
    // | VA.x | VB.x | VA.y | VB.y |
    c.blendps(v, vt, imm(0xA));
    e.update_vr_value(vd, v);
  } else {
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  e.TraceVR(vd, va, vb);
  return 0;
}
XEEMITTER(vmrghw,         0x1000008C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vmrghw_(e, c, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vmrghw128,      VX128(6, 768),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vmrghw_(e, c, VX128_VD128, VX128_VA128, VX128_VB128);
}

XEEMITTER(vmrglb,         0x1000010C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmrglh,         0x1000014C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmrglw_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD.x) = (VA.z)
  // (VD.y) = (VB.z)
  // (VD.z) = (VA.w)
  // (VD.w) = (VB.w)
  if (e.cpu_feature_mask() & kX86FeatureSse41) {
    // | VA.z | VA.z | VA.w | VA.w |
    XmmVar v(c.newXmmVar());
    c.movaps(v, e.vr_value(va));
    c.shufps(v, v, imm(0xFA));
    // | VB.z | VB.z | VB.w | VB.w |
    XmmVar vt(c.newXmmVar());
    c.movaps(vt, e.vr_value(vb));
    c.shufps(vt, vt, imm(0xFA));
    // | VA.z | VB.z | VA.w | VB.w |
    c.blendps(v, vt, imm(0xA));
    e.update_vr_value(vd, v);
  } else {
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  e.TraceVR(vd, va, vb);
  return 0;
}
XEEMITTER(vmrglw,         0x1000018C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vmrglw_(e, c, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vmrglw128,      VX128(6, 832),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vmrglw_(e, c, VX128_VD128, VX128_VA128, VX128_VB128);
}

XEEMITTER(vmsummbm,       0x10000025, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmsumshm,       0x10000028, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmsumshs,       0x10000029, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmsumubm,       0x10000024, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmsumuhm,       0x10000026, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmsumuhs,       0x10000027, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmsum3fp128,    VX128(5, 400),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // Dot product XYZ.
  // (VD.xyzw) = (VA.x * VB.x) + (VA.y * VB.y) + (VA.z * VB.z)
  if (e.cpu_feature_mask() & kX86FeatureSse41) {
    // SSE4.1 required.
    // Rumor is this is the same on older processors and way faster on new
    // ones (post 2011ish).
    XmmVar v(c.newXmmVar());
    c.movaps(v, e.vr_value(VX128_VA128));
    c.dpps(v, e.vr_value(VX128_VB128), imm(0x7F));
    e.update_vr_value(VX128_VD128, v);
  } else {
    //XmmVar v(c.newXmmVar());
    //c.movaps(v, e.vr_value(va));
    //c.mulps(v, e.vr_value(vb));
    //// TODO(benvanik): need to zero W
    //c.haddps(v, v);
    //c.haddps(v, v);
    //c.pshufd(v, v, imm(0));
    //e.update_vr_value(vd, v);
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
  e.TraceVR(VX128_VD128, VX128_VA128, VX128_VB128);
  return 0;
}

XEEMITTER(vmsum4fp128,    VX128(5, 464),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // Dot product XYZW.
  // (VD.xyzw) = (VA.x * VB.x) + (VA.y * VB.y) + (VA.z * VB.z) + (VA.w * VB.w)
  if (e.cpu_feature_mask() & kX86FeatureSse41) {
    // SSE4.1 required.
    // Rumor is this is the same on older processors and way faster on new
    // ones (post 2011ish).
    XmmVar v(c.newXmmVar());
    c.movaps(v, e.vr_value(VX128_VA128));
    c.dpps(v, e.vr_value(VX128_VB128), imm(0xFF));
    e.update_vr_value(VX128_VD128, v);
  } else {
    XmmVar v(c.newXmmVar());
    c.movaps(v, e.vr_value(VX128_VA128));
    c.mulps(v, e.vr_value(VX128_VB128));
    c.haddps(v, v);
    c.haddps(v, v);
    c.pshufd(v, v, imm(0));
    e.update_vr_value(VX128_VD128, v);
  }
  e.TraceVR(VX128_VD128, VX128_VA128, VX128_VB128);
  return 0;
}

XEEMITTER(vmulesb,        0x10000308, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmulesh,        0x10000348, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmuleub,        0x10000208, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmuleuh,        0x10000248, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmulosb,        0x10000108, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmulosh,        0x10000148, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmuloub,        0x10000008, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmulouh,        0x10000048, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmulfp128,      VX128(5, 144),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // (VD) <- (VA) * (VB) (4 x fp)
  XmmVar v(c.newXmmVar());
  c.movaps(v, e.vr_value(VX128_VA128));
  c.mulps(v, e.vr_value(VX128_VB128));
  e.update_vr_value(VX128_VD128, v);
  e.TraceVR(VX128_VD128, VX128_VA128, VX128_VB128);
  return 0;
}

int InstrEmit_vnmsubfp_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc) {
  // (VD) <- -(((VA) * (VC)) - (VB))
  // NOTE: only one rounding should take place, but that's hard...
  // This really needs VFNMSUB132PS/VFNMSUB213PS/VFNMSUB231PS but that's AVX.
  XmmVar v(c.newXmmVar());
  c.movaps(v, e.vr_value(va));
  c.mulps(v, e.vr_value(vc));
  c.subps(v, e.vr_value(vb));
  // *=-1
  GpVar sign_v(c.newGpVar());
  c.mov(sign_v, imm(0xBF7FFFFC)); // -1.0
  XmmVar sign(c.newXmmVar());
  c.movd(sign, sign_v.r32());
  c.shufps(sign, sign, imm(0));
  c.mulps(v, sign);
  e.update_vr_value(vd, v);
  e.TraceVR(vd, va, vb, vc);
  return 0;
}
XEEMITTER(vnmsubfp,       0x1000002F, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vnmsubfp_(e, c, i.VXA.VD, i.VXA.VA, i.VXA.VB, i.VXA.VC);
}
XEEMITTER(vnmsubfp128,    VX128(5, 336),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vnmsubfp_(e, c, VX128_VD128, VX128_VA128, VX128_VB128, VX128_VD128);
}

int InstrEmit_vnor_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t va, uint32_t vb) {
  // VD <- ¬((VA) | (VB))
  XmmVar v(c.newXmmVar());
  c.movaps(v, e.vr_value(vb));
  c.por(v, e.vr_value(va));
  XmmVar t(c.newXmmVar());
  c.pcmpeqd(t, t); // 0xFFFF....
  c.pxor(v, t);
  e.update_vr_value(vd, v);
  e.TraceVR(vd, va, vb);
  return 0;
}
XEEMITTER(vnor,           0x10000504, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vnor_(e, c, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vnor128,        VX128(5, 656),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vnor_(e, c, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vor_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t va, uint32_t vb) {
  // VD <- (VA) | (VB)
  if (va == vb) {
    // Copy VA==VB into VD.
    e.update_vr_value(vd, e.vr_value(va));
  } else {
    XmmVar v(c.newXmmVar());
    c.movaps(v, e.vr_value(vb));
    c.por(v, e.vr_value(va));
    e.update_vr_value(vd, v);
  }
  e.TraceVR(vd, va, vb);
  return 0;
}
XEEMITTER(vor,            0x10000484, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vor_(e, c, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vor128,         VX128(5, 720),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vor_(e, c, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vperm_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vperm,          0x1000002B, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vperm_(e, c, i.VXA.VD, i.VXA.VA, i.VXA.VB, i.VXA.VC);
}
XEEMITTER(vperm128,       VX128_2(5, 0),    VX128_2)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vperm_(e, c, VX128_2_VD128, VX128_2_VA128, VX128_2_VB128, VX128_2_VC);
}

XEEMITTER(vpermwi128,     VX128_P(6, 528),  VX128_P)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // (VD.x) = (VB.uimm[6-7])
  // (VD.y) = (VB.uimm[4-5])
  // (VD.z) = (VB.uimm[2-3])
  // (VD.w) = (VB.uimm[0-1])
  const uint32_t vd = i.VX128_P.VD128l | (i.VX128_P.VD128h << 5);
  const uint32_t vb = i.VX128_P.VB128l | (i.VX128_P.VB128h << 5);
  uint32_t uimm = i.VX128_P.PERMl | (i.VX128_P.PERMh << 5);
  // SHUFPS is flipped -- 0-1 selects X, 2-3 selects Y, etc.
  uimm = ((uimm & 0x03) << 6) |
         ((uimm & 0x0C) << 2) |
         ((uimm & 0x30) >> 2) |
         ((uimm & 0xC0) >> 6);
  XmmVar v(c.newXmmVar());
  c.movaps(v, e.vr_value(vb));
  c.shufps(v, v, imm(uimm));
  e.update_vr_value(vd, v);
  e.TraceVR(vd, vb);
  return 0;
}

XEEMITTER(vpkpx,          0x1000030E, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkshss,        0x1000018E, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkshss128,     VX128(5, 512),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkswss,        0x100001CE, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkswss128,     VX128(5, 640),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkswus,        0x1000014E, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkswus128,     VX128(5, 704),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkuhum,        0x1000000E, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkuhum128,     VX128(5, 768),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkuhus,        0x1000008E, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkuhus128,     VX128(5, 832),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkshus,        0x1000010E, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkshus128,     VX128(5, 576),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkuwum,        0x1000004E, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkuwum128,     VX128(5, 896),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkuwus,        0x100000CE, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkuwus128,     VX128(5, 960),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkd3d128,      VX128_4(6, 1552), VX128_4)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrefp,          0x1000010A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vrefp128,       VX128_3(6, 1584), VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrfim,          0x100002CA, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vrfim128,       VX128_3(6, 816),  VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrfin,          0x1000020A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vrfin128,       VX128_3(6, 880),  VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrfip,          0x1000028A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vrfip128,       VX128_3(6, 944),  VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrfiz,          0x1000024A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vrfiz128,       VX128_3(6, 1008), VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrlb,           0x10000004, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrlh,           0x10000044, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrlw,           0x10000084, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vrlw128,        VX128(6, 80),     VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrlimi128,      VX128_4(6, 1808), VX128_4)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  const uint32_t vd = i.VX128_4.VD128l | (i.VX128_4.VD128h << 5);
  const uint32_t vb = i.VX128_4.VB128l | (i.VX128_4.VB128h << 5);
  uint32_t x = i.VX128_4.IMM;
  uint32_t y = i.VX128_4.z;
  XmmVar v(c.newXmmVar());
  c.movaps(v, e.vr_value(vb));
  // This is just a fancy permute.
  // X Y Z W, rotated left by 2 = Z W X Y
  // Then mask select the results into the dest.
  // Sometimes rotation is zero, so fast path.
  if (y) {
    switch (y) {
    case 1:
      // X Y Z W -> Y Z W X
      c.shufps(v, v, imm(0x39));
      break;
    case 2:
      // X Y Z W -> Z W X Y
      c.shufps(v, v, imm(0x4E));
      break;
    case 3:
      // X Y Z W -> W X Y Z
      c.shufps(v, v, imm(0x93));
      break;
    default: XEASSERTALWAYS(); return 1;
    }
  }
  uint32_t blend_mask =
      (((x & 0x08) ? 1 : 0) << 0) |
      (((x & 0x04) ? 1 : 0) << 1) |
      (((x & 0x02) ? 1 : 0) << 2) |
      (((x & 0x01) ? 1 : 0) << 3);
  // Blending src into dest, so invert.
  blend_mask = (~blend_mask) & 0xF;
  c.blendps(v, e.vr_value(vd), imm(blend_mask));
  e.update_vr_value(vd, v);
  e.TraceVR(vd, vb);
  return 0;
}

int InstrEmit_vrsqrtefp_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t vb) {
  // (VD) <- 1 / sqrt(VB)
  // There are a lot of rules in the Altivec_PEM docs for handlings that
  // result in nan/infinity/etc. They are ignored here. I hope games would
  // never rely on them.
  XmmVar v(c.newXmmVar());
  c.rsqrtps(v, e.vr_value(vb));
  e.update_vr_value(vd, v);
  e.TraceVR(vd, vb);
  return 0;
}
XEEMITTER(vrsqrtefp,      0x1000014A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vrsqrtefp_(e, c, i.VX.VD, i.VX.VB);
}
XEEMITTER(vrsqrtefp128,   VX128_3(6, 1648), VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vrsqrtefp_(e, c, VX128_3_VD128, VX128_3_VB128);
}

XEEMITTER(vsel,           0x1000002A, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vsel128,        VX128(5, 848),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsl,            0x100001C4, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vslb,           0x10000104, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsldoi,         0x1000002C, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vsldoi128,      VX128_5(4, 16),   VX128_5)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vslh,           0x10000144, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vslo,           0x1000040C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vslo128,        VX128(5, 912),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vslw_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t va, uint32_t vb) {
  // VA = |xxxxx|yyyyy|zzzzz|wwwww|
  // VB = |...sh|...sh|...sh|...sh|
  // VD = |x<<sh|y<<sh|z<<sh|w<<sh|
  // There is no SSE op to do this, so we have to do each individually.
  // TODO(benvanik): update to do in two ops by doing 0/2 and 1/3.
  GpVar sh(c.newGpVar());
  GpVar vt(c.newGpVar());
  XmmVar v(c.newXmmVar());
  // 0
  c.pextrb(sh, e.vr_value(vb), imm(0));
  c.and_(sh, imm(0x1F));
  c.pextrd(vt, e.vr_value(va), imm(0));
  c.shl(vt, sh);
  c.pinsrd(v, vt.r32(), imm(0));
  // 1
  c.pextrb(sh, e.vr_value(vb), imm(1 * 4));
  c.and_(sh, imm(0x1F));
  c.pextrd(vt, e.vr_value(va), imm(1));
  c.shl(vt, sh);
  c.pinsrd(v, vt.r32(), imm(1));
  // 2
  c.pextrb(sh, e.vr_value(vb), imm(2 * 4));
  c.and_(sh, imm(0x1F));
  c.pextrd(vt, e.vr_value(va), imm(2));
  c.shl(vt, sh);
  c.pinsrd(v, vt.r32(), imm(2));
  // 3
  c.pextrb(sh, e.vr_value(vb), imm(3 * 4));
  c.and_(sh, imm(0x1F));
  c.pextrd(vt, e.vr_value(va), imm(3));
  c.shl(vt, sh);
  c.pinsrd(v, vt.r32(), imm(3));
  e.update_vr_value(vd, v);
  e.TraceVR(vd, va, vb);
  return 0;
}
XEEMITTER(vslw,           0x10000184, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vslw_(e, c, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vslw128,        VX128(6, 208),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vslw_(e, c, VX128_VD128, VX128_VA128, VX128_VB128);
}

XEEMITTER(vspltb,         0x1000020C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsplth,         0x1000024C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vspltisb,       0x1000030C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vspltish,       0x1000034C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vspltisw_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t uimm) {
  // (VD.xyzw) <- sign_extend(uimm)
  XmmVar v(c.newXmmVar());
  if (uimm) {
    // Sign extend from 5bits -> 32 and load.
    int32_t simm = (uimm & 0x10) ? (uimm | 0xFFFFFFF0) : uimm;
    GpVar simm_v(c.newGpVar());
    c.mov(simm_v, imm(simm));
    c.movd(v, simm_v.r32());
    c.pshufd(v, v, imm(0));
  } else {
    // Zero out the register.
    c.xorps(v, v);
  }
  e.update_vr_value(vd, v);
  e.TraceVR(vd);
  return 0;
}
XEEMITTER(vspltisw,       0x1000038C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vspltisw_(e, c, i.VX.VD, i.VX.VA);
}
XEEMITTER(vspltisw128,    VX128_3(6, 1904), VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vspltisw_(e, c, VX128_3_VD128, VX128_3_IMM);
}

int InstrEmit_vspltw_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t vb, uint32_t uimm) {
  // (VD.xyzw) <- (VB.uimm)
  XmmVar v(c.newXmmVar());
  c.movaps(v, e.vr_value(vb));
  switch (uimm) {
  case 0: // x
    c.shufps(v, v, imm(0x00));
    break;
  case 1: // y
    c.shufps(v, v, imm(0x55));
    break;
  case 2: // z
    c.shufps(v, v, imm(0xAA));
    break;
  case 3: // w
    c.shufps(v, v, imm(0xFF));
    break;
  }
  e.update_vr_value(vd, v);
  e.TraceVR(vd, vb);
  return 0;
}
XEEMITTER(vspltw,         0x1000028C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vspltw_(e, c, i.VX.VD, i.VX.VB, i.VX.VA);
}
XEEMITTER(vspltw128,      VX128_3(6, 1840), VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vspltw_(e, c, VX128_3_VD128, VX128_3_VB128, VX128_3_IMM);
}

XEEMITTER(vsr,            0x100002C4, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsrab,          0x10000304, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsrah,          0x10000344, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsraw,          0x10000384, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vsraw128,       VX128(6, 336),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsrb,           0x10000204, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsrh,           0x10000244, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsro,           0x1000044C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vsro128,        VX128(5, 976),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsrw,           0x10000284, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vsrw128,        VX128(6, 464),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubcuw,        0x10000580, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vsubfp_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD) <- (VA) - (VB) (4 x fp)
  XmmVar v(c.newXmmVar());
  c.movaps(v, e.vr_value(va));
  c.subps(v, e.vr_value(vb));
  e.update_vr_value(vd, v);
  e.TraceVR(vd, va, vb);
  return 0;
}
XEEMITTER(vsubfp,         0x1000004A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vsubfp_(e, c, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vsubfp128,      VX128(5, 80),     VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vsubfp_(e, c, VX128_VD128, VX128_VA128, VX128_VB128);
}

XEEMITTER(vsubsbs,        0x10000700, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubshs,        0x10000740, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubsws,        0x10000780, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsububm,        0x10000400, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsububs,        0x10000600, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubuhm,        0x10000440, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubuhs,        0x10000640, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubuwm,        0x10000480, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubuws,        0x10000680, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsumsws,        0x10000788, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsum2sws,       0x10000688, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsum4sbs,       0x10000708, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsum4shs,       0x10000648, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsum4ubs,       0x10000608, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vupkhpx,        0x1000034E, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vupkhsb,        0x1000020E, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vupkhsb128,     VX128(6, 896),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vupkhsh,        0x1000024E, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vupklpx,        0x100003CE, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vupklsb,        0x1000028E, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vupklsb128,     VX128(6, 960),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vupklsh,        0x100002CE, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

__m128 half_to_float5_SSE2(__m128i h) {
#define SSE_CONST4(name, val) static const __declspec(align(16)) uint name[4] = { (val), (val), (val), (val) }
#define SSE_CONST(name) *(const __m128i *)&name
#define SSE_CONSTF(name) *(const __m128 *)&name
    SSE_CONST4(mask_nosign,         0x7fff);
    SSE_CONST4(magic,               (254 - 15) << 23);
    SSE_CONST4(was_infnan,          0x7bff);
    SSE_CONST4(exp_infnan,          255 << 23);
    __m128i mnosign     = SSE_CONST(mask_nosign);
    __m128i expmant     = _mm_and_si128(mnosign, h);
    __m128i justsign    = _mm_xor_si128(h, expmant);
    __m128i expmant2    = expmant; // copy (just here for counting purposes)
    __m128i shifted     = _mm_slli_epi32(expmant, 13);
    __m128  scaled      = _mm_mul_ps(_mm_castsi128_ps(shifted), *(const __m128 *)&magic);
    __m128i b_wasinfnan = _mm_cmpgt_epi32(expmant2, SSE_CONST(was_infnan));
    __m128i sign        = _mm_slli_epi32(justsign, 16);
    __m128  infnanexp   = _mm_and_ps(_mm_castsi128_ps(b_wasinfnan), SSE_CONSTF(exp_infnan));
    __m128  sign_inf    = _mm_or_ps(_mm_castsi128_ps(sign), infnanexp);
    __m128  final       = _mm_or_ps(scaled, sign_inf);
    // ~11 SSE2 ops.
    return final;
#undef SSE_CONST4
#undef CONST
#undef CONSTF
}

XEEMITTER(vupkd3d128,     VX128_3(6, 2032), VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // Can't find many docs on this. Best reference is
  // http://worldcraft.googlecode.com/svn/trunk/src/qylib/math/xmmatrix.inl,
  // which shows how it's used in some cases. Since it's all intrinsics,
  // finding it in code is pretty easy.
  const uint32_t vd = i.VX128_3.VD128l | (i.VX128_3.VD128h << 5);
  const uint32_t vb = i.VX128_3.VB128l | (i.VX128_3.VB128h << 5);
  const uint32_t type = i.VX128_3.IMM >> 2;
  XmmVar v(c.newXmmVar());
  GpVar gt(c.newGpVar());
  XmmVar vt(c.newXmmVar());
  switch (type) {
  case 0: // VPACK_D3DCOLOR
    {
      // http://hlssmod.net/he_code/public/pixelwriter.h
      // ARGB (WXYZ) -> RGBA (XYZW)
      // zzzzZZZZzzzzARGB
      c.movaps(vt, e.vr_value(vb));
      // zzzzZZZZzzzzARGB
      // 000R000G000B000A
      c.mov(gt, imm(
          ((1ull << 7) << 56) |
          ((1ull << 7) << 48) |
          ((1ull << 7) << 40) |
          ((0ull) << 32) | // B
          ((1ull << 7) << 24) |
          ((1ull << 7) << 16) |
          ((1ull << 7) << 8) |
          ((3ull) << 0)) // A
          ); // lo
      c.movq(v, gt);
      c.mov(gt, imm(
          ((1ull << 7) << 56) |
          ((1ull << 7) << 48) |
          ((1ull << 7) << 40) |
          ((2ull) << 32) | // R
          ((1ull << 7) << 24) |
          ((1ull << 7) << 16) |
          ((1ull << 7) << 8) |
          ((1ull) << 0)) // G
          ); // hi
      c.pinsrq(v, gt, imm(1));
      c.pshufb(vt, v);
      // {256*R.0, 256*G.0, 256*B.0, 256*A.0}
      c.cvtdq2ps(v, vt);
      // {R.0, G.0, B.0 A.0}
      // 1/256 = 0.00390625 = 0x3B800000
      c.mov(gt, imm(0x3B800000));
      c.movd(vt, gt.r32());
      c.shufps(vt, vt, imm(0));
      c.mulps(v, vt);
    }
    break;
  case 1: // VPACK_NORMSHORT2
    {
      // (VD.x) = 3.0 + (VB.x)*2^-22
      // (VD.y) = 3.0 + (VB.y)*2^-22
      // (VD.z) = 0.0
      // (VD.w) = 3.0
      c.movaps(vt, e.vr_value(vb));
      c.xorps(v, v);
      // VB.x|VB.y|0|0
      c.shufps(vt, v, imm(0x10));
      // *=2^-22
      c.mov(gt, imm(0x34800000));
      c.pinsrd(v, gt.r32(), imm(0));
      c.pinsrd(v, gt.r32(), imm(1));
      c.mulps(v, vt);
      // {3.0, 3.0, 0, 1.0}
      c.xorps(vt, vt);
      c.mov(gt, imm(0x40400000));
      c.pinsrd(vt, gt.r32(), imm(0));
      c.pinsrd(vt, gt.r32(), imm(1));
      c.mov(gt, imm(0x3F800000));
      c.pinsrd(vt, gt.r32(), imm(3));
      c.addps(v, vt);
    }
    break;
  case 3: // VPACK_... 2 FLOAT16s
    {
      // (VD.x) = fixed_16_to_32(VB.x (low))
      // (VD.y) = fixed_16_to_32(VB.x (high))
      // (VD.z) = 0.0
      // (VD.w) = 1.0
      // 1 bit sign, 5 bit exponent, 10 bit mantissa
      // D3D10 half float format
      // TODO(benvanik): fixed_16_to_32 in SSE?
      // TODO(benvanik): http://blogs.msdn.com/b/chuckw/archive/2012/09/11/directxmath-f16c-and-fma.aspx
      // Use _mm_cvtph_ps -- requires very modern processors (SSE5+)
      // Unpacking half floats: http://fgiesen.wordpress.com/2012/03/28/half-to-float-done-quic/
      // Packing half floats: https://gist.github.com/rygorous/2156668
      // Load source, move from tight pack of X16Y16.... to X16...Y16...
      // Also zero out the high end.
      c.int3();
      c.movaps(vt, e.vr_value(vb));
      c.save(vt);
      c.lea(gt, vt.m128());
      X86CompilerFuncCall* call = c.call(half_to_float5_SSE2);
      uint32_t args[] = {kX86VarTypeGpq};
      call->setPrototype(kX86FuncConvDefault, kX86VarTypeXmm, args, XECOUNT(args));
      call->setArgument(0, gt);
      call->setReturn(v);
      // Select XY00.
      c.xorps(vt, vt);
      c.shufps(v, vt, imm(0x04));
      // {0.0, 0.0, 0.0, 1.0}
      c.mov(gt, imm(0x3F800000));
      c.pinsrd(v, gt.r32(), imm(3));
    }
    break;
  default:
    XEASSERTALWAYS();
    return 1;
  }
  e.update_vr_value(vd, v);
  e.TraceVR(vd, vb);
  return 0;
}

int InstrEmit_vxor_(X64Emitter& e, X86Compiler& c, uint32_t vd, uint32_t va, uint32_t vb) {
  // VD <- (VA) ^ (VB)
  XmmVar v(c.newXmmVar());
  if (va == vb) {
    // Fast clear.
    c.xorps(v, v);
  } else {
    c.movaps(v, e.vr_value(vb));
    c.pxor(v, e.vr_value(va));
  }
  e.update_vr_value(vd, v);
  e.TraceVR(vd, va, vb);
  return 0;
}
XEEMITTER(vxor,           0x100004C4, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vxor_(e, c, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vxor128,        VX128(5, 784),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_vxor_(e, c, VX128_VD128, VX128_VA128, VX128_VB128);
}


void X64RegisterEmitCategoryAltivec() {
  XEREGISTERINSTR(dst,            0x7C0002AC);
  XEREGISTERINSTR(dstst,          0x7C0002EC);
  XEREGISTERINSTR(dss,            0x7C00066C);
  XEREGISTERINSTR(lvebx,          0x7C00000E);
  XEREGISTERINSTR(lvehx,          0x7C00004E);
  XEREGISTERINSTR(lvewx,          0x7C00008E);
  XEREGISTERINSTR(lvewx128,       VX128_1(4, 131));
  XEREGISTERINSTR(lvsl,           0x7C00000C);
  XEREGISTERINSTR(lvsl128,        VX128_1(4, 3));
  XEREGISTERINSTR(lvsr,           0x7C00004C);
  XEREGISTERINSTR(lvsr128,        VX128_1(4, 67));
  XEREGISTERINSTR(lvx,            0x7C0000CE);
  XEREGISTERINSTR(lvx128,         VX128_1(4, 195));
  XEREGISTERINSTR(lvxl,           0x7C0002CE);
  XEREGISTERINSTR(lvxl128,        VX128_1(4, 707));
  XEREGISTERINSTR(stvebx,         0x7C00010E);
  XEREGISTERINSTR(stvehx,         0x7C00014E);
  XEREGISTERINSTR(stvewx,         0x7C00018E);
  XEREGISTERINSTR(stvewx128,      VX128_1(4, 387));
  XEREGISTERINSTR(stvx,           0x7C0001CE);
  XEREGISTERINSTR(stvx128,        VX128_1(4, 451));
  XEREGISTERINSTR(stvxl,          0x7C0003CE);
  XEREGISTERINSTR(stvxl128,       VX128_1(4, 963));
  XEREGISTERINSTR(lvlx,           0x7C00040E);
  XEREGISTERINSTR(lvlx128,        VX128_1(4, 1027));
  XEREGISTERINSTR(lvlxl,          0x7C00060E);
  XEREGISTERINSTR(lvlxl128,       VX128_1(4, 1539));
  XEREGISTERINSTR(lvrx,           0x7C00044E);
  XEREGISTERINSTR(lvrx128,        VX128_1(4, 1091));
  XEREGISTERINSTR(lvrxl,          0x7C00064E);
  XEREGISTERINSTR(lvrxl128,       VX128_1(4, 1603));
  XEREGISTERINSTR(stvlx,          0x7C00050E);
  XEREGISTERINSTR(stvlx128,       VX128_1(4, 1283));
  XEREGISTERINSTR(stvlxl,         0x7C00070E);
  XEREGISTERINSTR(stvlxl128,      VX128_1(4, 1795));
  XEREGISTERINSTR(stvrx,          0x7C00054E);
  XEREGISTERINSTR(stvrx128,       VX128_1(4, 1347));
  XEREGISTERINSTR(stvrxl,         0x7C00074E);
  XEREGISTERINSTR(stvrxl128,      VX128_1(4, 1859));

  XEREGISTERINSTR(mfvscr,         0x10000604);
  XEREGISTERINSTR(mtvscr,         0x10000644);
  XEREGISTERINSTR(vaddcuw,        0x10000180);
  XEREGISTERINSTR(vaddfp,         0x1000000A);
  XEREGISTERINSTR(vaddfp128,      VX128(5, 16));
  XEREGISTERINSTR(vaddsbs,        0x10000300);
  XEREGISTERINSTR(vaddshs,        0x10000340);
  XEREGISTERINSTR(vaddsws,        0x10000380);
  XEREGISTERINSTR(vaddubm,        0x10000000);
  XEREGISTERINSTR(vaddubs,        0x10000200);
  XEREGISTERINSTR(vadduhm,        0x10000040);
  XEREGISTERINSTR(vadduhs,        0x10000240);
  XEREGISTERINSTR(vadduwm,        0x10000080);
  XEREGISTERINSTR(vadduws,        0x10000280);
  XEREGISTERINSTR(vand,           0x10000404);
  XEREGISTERINSTR(vand128,        VX128(5, 528));
  XEREGISTERINSTR(vandc,          0x10000444);
  XEREGISTERINSTR(vandc128,       VX128(5, 592));
  XEREGISTERINSTR(vavgsb,         0x10000502);
  XEREGISTERINSTR(vavgsh,         0x10000542);
  XEREGISTERINSTR(vavgsw,         0x10000582);
  XEREGISTERINSTR(vavgub,         0x10000402);
  XEREGISTERINSTR(vavguh,         0x10000442);
  XEREGISTERINSTR(vavguw,         0x10000482);
  XEREGISTERINSTR(vcfsx,          0x1000034A);
  XEREGISTERINSTR(vcsxwfp128,     VX128_3(6, 688));
  XEREGISTERINSTR(vcfpsxws128,    VX128_3(6, 560));
  XEREGISTERINSTR(vcfux,          0x1000030A);
  XEREGISTERINSTR(vcuxwfp128,     VX128_3(6, 752));
  XEREGISTERINSTR(vcfpuxws128,    VX128_3(6, 624));
  XEREGISTERINSTR(vcmpbfp,        0x100003C6);
  XEREGISTERINSTR(vcmpbfp128,     VX128(6, 384));
  XEREGISTERINSTR(vcmpeqfp,       0x100000C6);
  XEREGISTERINSTR(vcmpeqfp128,    VX128(6, 0));
  XEREGISTERINSTR(vcmpgefp,       0x100001C6);
  XEREGISTERINSTR(vcmpgefp128,    VX128(6, 128));
  XEREGISTERINSTR(vcmpgtfp,       0x100002C6);
  XEREGISTERINSTR(vcmpgtfp128,    VX128(6, 256));
  XEREGISTERINSTR(vcmpgtsb,       0x10000306);
  XEREGISTERINSTR(vcmpgtsh,       0x10000346);
  XEREGISTERINSTR(vcmpgtsw,       0x10000386);
  XEREGISTERINSTR(vcmpequb,       0x10000006);
  XEREGISTERINSTR(vcmpgtub,       0x10000206);
  XEREGISTERINSTR(vcmpequh,       0x10000046);
  XEREGISTERINSTR(vcmpgtuh,       0x10000246);
  XEREGISTERINSTR(vcmpequw,       0x10000086);
  XEREGISTERINSTR(vcmpequw128,    VX128(6, 512));
  XEREGISTERINSTR(vcmpgtuw,       0x10000286);
  XEREGISTERINSTR(vctsxs,         0x100003CA);
  XEREGISTERINSTR(vctuxs,         0x1000038A);
  XEREGISTERINSTR(vexptefp,       0x1000018A);
  XEREGISTERINSTR(vexptefp128,    VX128_3(6, 1712));
  XEREGISTERINSTR(vlogefp,        0x100001CA);
  XEREGISTERINSTR(vlogefp128,     VX128_3(6, 1776));
  XEREGISTERINSTR(vmaddfp,        0x1000002E);
  XEREGISTERINSTR(vmaddfp128,     VX128(5, 208));
  XEREGISTERINSTR(vmaddcfp128,    VX128(5, 272));
  XEREGISTERINSTR(vmaxfp,         0x1000040A);
  XEREGISTERINSTR(vmaxfp128,      VX128(6, 640));
  XEREGISTERINSTR(vmaxsb,         0x10000102);
  XEREGISTERINSTR(vmaxsh,         0x10000142);
  XEREGISTERINSTR(vmaxsw,         0x10000182);
  XEREGISTERINSTR(vmaxub,         0x10000002);
  XEREGISTERINSTR(vmaxuh,         0x10000042);
  XEREGISTERINSTR(vmaxuw,         0x10000082);
  XEREGISTERINSTR(vmhaddshs,      0x10000020);
  XEREGISTERINSTR(vmhraddshs,     0x10000021);
  XEREGISTERINSTR(vminfp,         0x1000044A);
  XEREGISTERINSTR(vminfp128,      VX128(6, 704));
  XEREGISTERINSTR(vminsb,         0x10000302);
  XEREGISTERINSTR(vminsh,         0x10000342);
  XEREGISTERINSTR(vminsw,         0x10000382);
  XEREGISTERINSTR(vminub,         0x10000202);
  XEREGISTERINSTR(vminuh,         0x10000242);
  XEREGISTERINSTR(vminuw,         0x10000282);
  XEREGISTERINSTR(vmladduhm,      0x10000022);
  XEREGISTERINSTR(vmrghb,         0x1000000C);
  XEREGISTERINSTR(vmrghh,         0x1000004C);
  XEREGISTERINSTR(vmrghw,         0x1000008C);
  XEREGISTERINSTR(vmrghw128,      VX128(6, 768));
  XEREGISTERINSTR(vmrglb,         0x1000010C);
  XEREGISTERINSTR(vmrglh,         0x1000014C);
  XEREGISTERINSTR(vmrglw,         0x1000018C);
  XEREGISTERINSTR(vmrglw128,      VX128(6, 832));
  XEREGISTERINSTR(vmsummbm,       0x10000025);
  XEREGISTERINSTR(vmsumshm,       0x10000028);
  XEREGISTERINSTR(vmsumshs,       0x10000029);
  XEREGISTERINSTR(vmsumubm,       0x10000024);
  XEREGISTERINSTR(vmsumuhm,       0x10000026);
  XEREGISTERINSTR(vmsumuhs,       0x10000027);
  XEREGISTERINSTR(vmsum3fp128,    VX128(5, 400));
  XEREGISTERINSTR(vmsum4fp128,    VX128(5, 464));
  XEREGISTERINSTR(vmulesb,        0x10000308);
  XEREGISTERINSTR(vmulesh,        0x10000348);
  XEREGISTERINSTR(vmuleub,        0x10000208);
  XEREGISTERINSTR(vmuleuh,        0x10000248);
  XEREGISTERINSTR(vmulosb,        0x10000108);
  XEREGISTERINSTR(vmulosh,        0x10000148);
  XEREGISTERINSTR(vmuloub,        0x10000008);
  XEREGISTERINSTR(vmulouh,        0x10000048);
  XEREGISTERINSTR(vmulfp128,      VX128(5, 144));
  XEREGISTERINSTR(vnmsubfp,       0x1000002F);
  XEREGISTERINSTR(vnmsubfp128,    VX128(5, 336));
  XEREGISTERINSTR(vnor,           0x10000504);
  XEREGISTERINSTR(vnor128,        VX128(5, 656));
  XEREGISTERINSTR(vor,            0x10000484);
  XEREGISTERINSTR(vor128,         VX128(5, 720));
  XEREGISTERINSTR(vperm,          0x1000002B);
  XEREGISTERINSTR(vperm128,       VX128_2(5, 0));
  XEREGISTERINSTR(vpermwi128,     VX128_P(6, 528));
  XEREGISTERINSTR(vpkpx,          0x1000030E);
  XEREGISTERINSTR(vpkshss,        0x1000018E);
  XEREGISTERINSTR(vpkshss128,     VX128(5, 512));
  XEREGISTERINSTR(vpkshus,        0x1000010E);
  XEREGISTERINSTR(vpkshus128,     VX128(5, 576));
  XEREGISTERINSTR(vpkswss,        0x100001CE);
  XEREGISTERINSTR(vpkswss128,     VX128(5, 640));
  XEREGISTERINSTR(vpkswus,        0x1000014E);
  XEREGISTERINSTR(vpkswus128,     VX128(5, 704));
  XEREGISTERINSTR(vpkuhum,        0x1000000E);
  XEREGISTERINSTR(vpkuhum128,     VX128(5, 768));
  XEREGISTERINSTR(vpkuhus,        0x1000008E);
  XEREGISTERINSTR(vpkuhus128,     VX128(5, 832));
  XEREGISTERINSTR(vpkuwum,        0x1000004E);
  XEREGISTERINSTR(vpkuwum128,     VX128(5, 896));
  XEREGISTERINSTR(vpkuwus,        0x100000CE);
  XEREGISTERINSTR(vpkuwus128,     VX128(5, 960));
  XEREGISTERINSTR(vpkd3d128,      VX128_4(6, 1552));
  XEREGISTERINSTR(vrefp,          0x1000010A);
  XEREGISTERINSTR(vrefp128,       VX128_3(6, 1584));
  XEREGISTERINSTR(vrfim,          0x100002CA);
  XEREGISTERINSTR(vrfim128,       VX128_3(6, 816));
  XEREGISTERINSTR(vrfin,          0x1000020A);
  XEREGISTERINSTR(vrfin128,       VX128_3(6, 880));
  XEREGISTERINSTR(vrfip,          0x1000028A);
  XEREGISTERINSTR(vrfip128,       VX128_3(6, 944));
  XEREGISTERINSTR(vrfiz,          0x1000024A);
  XEREGISTERINSTR(vrfiz128,       VX128_3(6, 1008));
  XEREGISTERINSTR(vrlb,           0x10000004);
  XEREGISTERINSTR(vrlh,           0x10000044);
  XEREGISTERINSTR(vrlw,           0x10000084);
  XEREGISTERINSTR(vrlw128,        VX128(6, 80));
  XEREGISTERINSTR(vrlimi128,      VX128_4(6, 1808));
  XEREGISTERINSTR(vrsqrtefp,      0x1000014A);
  XEREGISTERINSTR(vrsqrtefp128,   VX128_3(6, 1648));
  XEREGISTERINSTR(vsel,           0x1000002A);
  XEREGISTERINSTR(vsel128,        VX128(5, 848));
  XEREGISTERINSTR(vsl,            0x100001C4);
  XEREGISTERINSTR(vslb,           0x10000104);
  XEREGISTERINSTR(vsldoi,         0x1000002C);
  XEREGISTERINSTR(vsldoi128,      VX128_5(4, 16));
  XEREGISTERINSTR(vslh,           0x10000144);
  XEREGISTERINSTR(vslo,           0x1000040C);
  XEREGISTERINSTR(vslo128,        VX128(5, 912));
  XEREGISTERINSTR(vslw,           0x10000184);
  XEREGISTERINSTR(vslw128,        VX128(6, 208));
  XEREGISTERINSTR(vspltb,         0x1000020C);
  XEREGISTERINSTR(vsplth,         0x1000024C);
  XEREGISTERINSTR(vspltisb,       0x1000030C);
  XEREGISTERINSTR(vspltish,       0x1000034C);
  XEREGISTERINSTR(vspltisw,       0x1000038C);
  XEREGISTERINSTR(vspltisw128,    VX128_3(6, 1904));
  XEREGISTERINSTR(vspltw,         0x1000028C);
  XEREGISTERINSTR(vspltw128,      VX128_3(6, 1840));
  XEREGISTERINSTR(vsr,            0x100002C4);
  XEREGISTERINSTR(vsrab,          0x10000304);
  XEREGISTERINSTR(vsrah,          0x10000344);
  XEREGISTERINSTR(vsraw,          0x10000384);
  XEREGISTERINSTR(vsraw128,       VX128(6, 336));
  XEREGISTERINSTR(vsrb,           0x10000204);
  XEREGISTERINSTR(vsrh,           0x10000244);
  XEREGISTERINSTR(vsro,           0x1000044C);
  XEREGISTERINSTR(vsro128,        VX128(5, 976));
  XEREGISTERINSTR(vsrw,           0x10000284);
  XEREGISTERINSTR(vsrw128,        VX128(6, 464));
  XEREGISTERINSTR(vsubcuw,        0x10000580);
  XEREGISTERINSTR(vsubfp,         0x1000004A);
  XEREGISTERINSTR(vsubfp128,      VX128(5, 80));
  XEREGISTERINSTR(vsubsbs,        0x10000700);
  XEREGISTERINSTR(vsubshs,        0x10000740);
  XEREGISTERINSTR(vsubsws,        0x10000780);
  XEREGISTERINSTR(vsububm,        0x10000400);
  XEREGISTERINSTR(vsububs,        0x10000600);
  XEREGISTERINSTR(vsubuhm,        0x10000440);
  XEREGISTERINSTR(vsubuhs,        0x10000640);
  XEREGISTERINSTR(vsubuwm,        0x10000480);
  XEREGISTERINSTR(vsubuws,        0x10000680);
  XEREGISTERINSTR(vsumsws,        0x10000788);
  XEREGISTERINSTR(vsum2sws,       0x10000688);
  XEREGISTERINSTR(vsum4sbs,       0x10000708);
  XEREGISTERINSTR(vsum4shs,       0x10000648);
  XEREGISTERINSTR(vsum4ubs,       0x10000608);
  XEREGISTERINSTR(vupkhpx,        0x1000034E);
  XEREGISTERINSTR(vupkhsb,        0x1000020E);
  XEREGISTERINSTR(vupkhsb128,     VX128(6, 896));
  XEREGISTERINSTR(vupkhsh,        0x1000024E);
  XEREGISTERINSTR(vupklpx,        0x100003CE);
  XEREGISTERINSTR(vupklsb,        0x1000028E);
  XEREGISTERINSTR(vupklsb128,     VX128(6, 960));
  XEREGISTERINSTR(vupklsh,        0x100002CE);
  XEREGISTERINSTR(vupkd3d128,     VX128_3(6, 2032));
  XEREGISTERINSTR(vxor,           0x100004C4);
  XEREGISTERINSTR(vxor128,        VX128(5, 784));
}


}  // namespace x64
}  // namespace cpu
}  // namespace xe
