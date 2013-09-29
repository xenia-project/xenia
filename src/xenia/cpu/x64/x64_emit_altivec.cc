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

XEEMITTER(lvewx,          0x7C00008E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lvewx128,       VX128_1(4, 131),  VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lvsl,           0x7C00000C, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lvsl128,        VX128_1(4, 3),    VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lvsr,           0x7C00004C, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lvsr128,        VX128_1(4, 67),   VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lvx,            0x7C0000CE, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  GpVar ea(c.newGpVar());
  c.mov(ea, e.gpr_value(i.X.RB));
  if (i.VX128_1.RA) {
    c.add(ea, e.gpr_value(i.X.RA));
  }
  XmmVar v = e.ReadMemoryXmm(i.address, ea, 4);
  e.update_vr_value(i.X.RT, v);

  return 0;
}

XEEMITTER(lvx128,         VX128_1(4, 195),  VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  const uint32_t vd = i.VX128_1.VD128l | (i.VX128_1.VD128h << 5);

  GpVar ea(c.newGpVar());
  c.mov(ea, e.gpr_value(i.VX128_1.RB));
  if (i.VX128_1.RA) {
    c.add(ea, e.gpr_value(i.VX128_1.RA));
  }
  XmmVar v = e.ReadMemoryXmm(i.address, ea, 4);
  e.update_vr_value(vd, v);

  return 0;
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

XEEMITTER(stvewx,         0x7C00018E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(stvewx128,      VX128_1(4, 387),  VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(stvx,           0x7C0001CE, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  GpVar ea(c.newGpVar());
  c.mov(ea, e.gpr_value(i.X.RB));
  if (i.X.RA) {
    c.add(ea, e.gpr_value(i.X.RA));
  }
  XmmVar v = e.vr_value(i.X.RT);
  e.WriteMemoryXmm(i.address, ea, 4, v);

  return 0;
}

XEEMITTER(stvx128,        VX128_1(4, 451),  VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  const uint32_t vd = i.VX128_1.VD128l | (i.VX128_1.VD128h << 5);

  GpVar ea(c.newGpVar());
  c.mov(ea, e.gpr_value(i.VX128_1.RB));
  if (i.X.RA) {
    c.add(ea, e.gpr_value(i.VX128_1.RA));
  }
  XmmVar v = e.vr_value(vd);
  e.WriteMemoryXmm(i.address, ea, 4, v);

  return 0;
}

XEEMITTER(stvxl,          0x7C0003CE, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvx(e, c, i);
}

XEEMITTER(stvxl128,       VX128_1(4, 963),  VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvx128(e, c, i);
}

XEEMITTER(lvlx,           0x7C00040E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lvlx128,        VX128_1(4, 1027), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lvlxl,          0x7C00060E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvlx(e, c, i);
}

XEEMITTER(lvlxl128,       VX128_1(4, 1539), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvlx128(e, c, i);
}

XEEMITTER(lvrx,           0x7C00044E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lvrx128,        VX128_1(4, 1091), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lvrxl,          0x7C00064E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvrx(e, c, i);
}

XEEMITTER(lvrxl128,       VX128_1(4, 1603), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_lvrx128(e, c, i);
}

XEEMITTER(stvlx,          0x7C00050E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(stvlx128,       VX128_1(4, 1283), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(stvlxl,         0x7C00070E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvlx(e, c, i);
}

XEEMITTER(stvlxl128,      VX128_1(4, 1795), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  return InstrEmit_stvlx128(e, c, i);
}

XEEMITTER(stvrx,          0x7C00054E, X   )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(stvrx128,       VX128_1(4, 1347), VX128_1)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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

XEEMITTER(vand,           0x10000404, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // VD <- (VA) & (VB)

  XmmVar v(c.newXmmVar());
  c.movq(v, e.vr_value(i.VX.VB));
  c.pand(v, e.vr_value(i.VX.VA));
  e.update_vr_value(i.VX.VD, v);

  return 0;
}

XEEMITTER(vand128,        VX128(5, 528),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // VD <- (VA) & (VB)

  const uint32_t vd = i.VX128.VD128l | (i.VX128.VD128h << 5);
  const uint32_t va = i.VX128.VA128l | (i.VX128.VA128h << 5) |
                                       (i.VX128.VA128H << 6);
  const uint32_t vb = i.VX128.VB128l | (i.VX128.VB128h << 5);

  XmmVar v(c.newXmmVar());
  c.movq(v, e.vr_value(vb));
  c.pand(v, e.vr_value(va));
  e.update_vr_value(vd, v);

  return 0;
}

XEEMITTER(vandc,          0x10000444, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // VD <- (VA) & ¬(VB)

  XmmVar v(c.newXmmVar());
  c.movq(v, e.vr_value(i.VX.VB));
  c.pandn(v, e.vr_value(i.VX.VA));
  e.update_vr_value(i.VX.VD, v);

  return 0;
}

XEEMITTER(vandc128,       VX128(5, 592),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // VD <- (VA) & ¬(VB)

  const uint32_t vd = i.VX128.VD128l | (i.VX128.VD128h << 5);
  const uint32_t va = i.VX128.VA128l | (i.VX128.VA128h << 5) |
                                       (i.VX128.VA128H << 6);
  const uint32_t vb = i.VX128.VB128l | (i.VX128.VB128h << 5);

  XmmVar v(c.newXmmVar());
  c.movq(v, e.vr_value(vb));
  c.pandn(v, e.vr_value(va));
  e.update_vr_value(vd, v);

  return 0;
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
  XEINSTRNOTIMPLEMENTED();
  return 1;
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

XEEMITTER(vcmpbfp,        0x100003C6, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpbfp128,     VX128(6, 384),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpbfp_c,      0x100007C6, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpbfp128c,    VX128(6, 448),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpeqfp,       0x100000C6, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpeqfp128,    VX128(6, 0),      VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpeqfp_c,     0x100004C6, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpeqfp128c,   VX128(6, 64),     VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpequb,       0x10000006, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpequb_c,     0x10000406, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpequh,       0x10000046, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpequh_c,     0x10000446, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpequw,       0x10000086, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpequw128,    VX128(6, 512),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpequw_c,     0x10000486, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpequw128c,   VX128(6, 576),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgefp,       0x100001C6, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgefp128,    VX128(6, 128),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgefp_c,     0x100005C6, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgefp128c,   VX128(6, 192),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtfp,       0x100002C6, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtfp128,    VX128(6, 256),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtfp_c,     0x100006C6, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtfp128c,   VX128(6, 320),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtsb,       0x10000306, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtsb_c,     0x10000706, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtsh,       0x10000346, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtsh_c,     0x10000746, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtsw,       0x10000386, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtsw_c,     0x10000786, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtub,       0x10000206, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtub_c,     0x10000606, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtuh,       0x10000246, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtuh_c,     0x10000646, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtuw,       0x10000286, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcmpgtuw_c,     0x10000686, VXR )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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

XEEMITTER(vmaddfp,        0x1000002E, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmaddfp128,     VX128(5, 208),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmaddcfp128,    VX128(5, 272),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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

XEEMITTER(vmrghw,         0x1000008C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmrghw128,      VX128(6, 768),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmrglb,         0x1000010C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmrglh,         0x1000014C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmrglw,         0x1000018C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmrglw128,      VX128(6, 832),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmsum4fp128,    VX128(5, 464),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vnmsubfp,       0x1000002F, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vnmsubfp128,    VX128(5, 336),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vnor,           0x10000504, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // VD <- ¬((VA) | (VB))

  XmmVar v(c.newXmmVar());
  c.movq(v, e.vr_value(i.VX.VB));
  c.por(v, e.vr_value(i.VX.VA));
  XmmVar t(c.newXmmVar());
  c.pcmpeqd(t, t); // 0xFFFF....
  c.pxor(v, t);
  e.update_vr_value(i.VX.VD, v);

  return 0;
}

XEEMITTER(vnor128,        VX128(5, 656),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // VD <- ¬((VA) | (VB))

  const uint32_t vd = i.VX128.VD128l | (i.VX128.VD128h << 5);
  const uint32_t va = i.VX128.VA128l | (i.VX128.VA128h << 5) |
                                       (i.VX128.VA128H << 6);
  const uint32_t vb = i.VX128.VB128l | (i.VX128.VB128h << 5);

  XmmVar v(c.newXmmVar());
  c.movq(v, e.vr_value(vb));
  c.por(v, e.vr_value(va));
  XmmVar t(c.newXmmVar());
  c.pcmpeqd(t, t); // 0xFFFF....
  c.pxor(v, t);
  e.update_vr_value(vd, v);

  return 0;
}

XEEMITTER(vor,            0x10000484, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // VD <- (VA) | (VB)

  XmmVar v(c.newXmmVar());
  c.movq(v, e.vr_value(i.VX.VB));
  c.por(v, e.vr_value(i.VX.VA));
  e.update_vr_value(i.VX.VD, v);

  return 0;
}

XEEMITTER(vor128,         VX128(5, 720),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // VD <- (VA) | (VB)

  const uint32_t vd = i.VX128.VD128l | (i.VX128.VD128h << 5);
  const uint32_t va = i.VX128.VA128l | (i.VX128.VA128h << 5) |
                                       (i.VX128.VA128H << 6);
  const uint32_t vb = i.VX128.VB128l | (i.VX128.VB128h << 5);

  XmmVar v(c.newXmmVar());
  c.movq(v, e.vr_value(vb));
  c.por(v, e.vr_value(va));
  e.update_vr_value(vd, v);

  return 0;
}

XEEMITTER(vperm,          0x1000002B, VXA )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vperm128,       VX128_2(5, 0),    VX128_2)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpermwi128,     VX128_P(6, 528),  VX128_P)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrsqrtefp,      0x1000014A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrsqrtefp128,   VX128_3(6, 1648), VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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

XEEMITTER(vslw,           0x10000184, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vslw128,        VX128(6, 208),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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

XEEMITTER(vspltisw,       0x1000038C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vspltisw128,    VX128_3(6, 1904), VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vspltw,         0x1000028C, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vspltw128,      VX128_3(6, 1840), VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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

XEEMITTER(vsubfp,         0x1000004A, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubfp128,      VX128(5, 80),     VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
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

XEEMITTER(vupkd3d128,     VX128_3(6, 2032), VX128_3)(X64Emitter& e, X86Compiler& c, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vxor,           0x100004C4, VX  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // VD <- (VA) ^ (VB)

  XmmVar v(c.newXmmVar());
  c.movq(v, e.vr_value(i.VX.VB));
  c.pxor(v, e.vr_value(i.VX.VA));
  e.update_vr_value(i.VX.VD, v);

  return 0;
}

XEEMITTER(vxor128,        VX128(5, 784),    VX128  )(X64Emitter& e, X86Compiler& c, InstrData& i) {
  // VD <- (VA) ^ (VB)

  const uint32_t vd = i.VX128.VD128l | (i.VX128.VD128h << 5);
  const uint32_t va = i.VX128.VA128l | (i.VX128.VA128h << 5) |
                                       (i.VX128.VA128H << 6);
  const uint32_t vb = i.VX128.VB128l | (i.VX128.VB128h << 5);

  XmmVar v(c.newXmmVar());
  c.movq(v, e.vr_value(vb));
  c.pxor(v, e.vr_value(va));
  e.update_vr_value(vd, v);

  return 0;
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
  XEREGISTERINSTR(vcmpbfp_c,      0x100007C6);
  XEREGISTERINSTR(vcmpbfp128c,    VX128(6, 448));
  XEREGISTERINSTR(vcmpeqfp,       0x100000C6);
  XEREGISTERINSTR(vcmpeqfp128,    VX128(6, 0));
  XEREGISTERINSTR(vcmpeqfp_c,     0x100004C6);
  XEREGISTERINSTR(vcmpeqfp128c,   VX128(6, 64));
  XEREGISTERINSTR(vcmpequb,       0x10000006);
  XEREGISTERINSTR(vcmpequb_c,     0x10000406);
  XEREGISTERINSTR(vcmpequh,       0x10000046);
  XEREGISTERINSTR(vcmpequh_c,     0x10000446);
  XEREGISTERINSTR(vcmpequw,       0x10000086);
  XEREGISTERINSTR(vcmpequw128,    VX128(6, 512));
  XEREGISTERINSTR(vcmpequw_c,     0x10000486);
  XEREGISTERINSTR(vcmpequw128c,   VX128(6, 576));
  XEREGISTERINSTR(vcmpgefp,       0x100001C6);
  XEREGISTERINSTR(vcmpgefp128,    VX128(6, 128));
  XEREGISTERINSTR(vcmpgefp_c,     0x100005C6);
  XEREGISTERINSTR(vcmpgefp128c,   VX128(6, 192));
  XEREGISTERINSTR(vcmpgtfp,       0x100002C6);
  XEREGISTERINSTR(vcmpgtfp128,    VX128(6, 256));
  XEREGISTERINSTR(vcmpgtfp_c,     0x100006C6);
  XEREGISTERINSTR(vcmpgtfp128c,   VX128(6, 320));
  XEREGISTERINSTR(vcmpgtsb,       0x10000306);
  XEREGISTERINSTR(vcmpgtsb_c,     0x10000706);
  XEREGISTERINSTR(vcmpgtsh,       0x10000346);
  XEREGISTERINSTR(vcmpgtsh_c,     0x10000746);
  XEREGISTERINSTR(vcmpgtsw,       0x10000386);
  XEREGISTERINSTR(vcmpgtsw_c,     0x10000786);
  XEREGISTERINSTR(vcmpgtub,       0x10000206);
  XEREGISTERINSTR(vcmpgtub_c,     0x10000606);
  XEREGISTERINSTR(vcmpgtuh,       0x10000246);
  XEREGISTERINSTR(vcmpgtuh_c,     0x10000646);
  XEREGISTERINSTR(vcmpgtuw,       0x10000286);
  XEREGISTERINSTR(vcmpgtuw_c,     0x10000686);
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
