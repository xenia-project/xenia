/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/ppc/ppc_disasm-private.h>


using namespace alloy::frontend::ppc;


namespace alloy {
namespace frontend {
namespace ppc {


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


namespace {

int GeneralVXA(InstrData& i, InstrDisasm& d) {
  d.AddRegOperand(InstrRegister::kVMX, i.VXA.VD, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, i.VXA.VA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, i.VXA.VB, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, i.VXA.VC, InstrRegister::kRead);
  return d.Finish();
}

int GeneralVX128(InstrData& i, InstrDisasm& d) {
  const uint32_t vd = i.VX128.VD128l | (i.VX128.VD128h << 5);
  const uint32_t va = i.VX128.VA128l | (i.VX128.VA128h << 5) |
                                       (i.VX128.VA128H << 6);
  const uint32_t vb = i.VX128.VB128l | (i.VX128.VB128h << 5);
  d.AddRegOperand(InstrRegister::kVMX, vd, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, va, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, vb, InstrRegister::kRead);
  return d.Finish();
}

int GeneralX(InstrData& i, InstrDisasm& d, bool store) {
  d.AddRegOperand(InstrRegister::kVMX, i.X.RT,
      store ? InstrRegister::kRead : InstrRegister::kWrite);
  if (i.X.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.X.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.X.RB, InstrRegister::kRead);
  return d.Finish();
}

int GeneralVX128_1(InstrData& i, InstrDisasm& d, bool store) {
  const uint32_t vd = i.VX128_1.VD128l | (i.VX128_1.VD128h << 5);
  d.AddRegOperand(InstrRegister::kVMX, vd,
      store ? InstrRegister::kRead : InstrRegister::kWrite);
  if (i.VX128_1.RA) {
    d.AddRegOperand(InstrRegister::kGPR, i.VX128_1.RA, InstrRegister::kRead);
  } else {
    d.AddUImmOperand(0, 1);
  }
  d.AddRegOperand(InstrRegister::kGPR, i.VX128_1.RB, InstrRegister::kRead);
  return d.Finish();
}

int GeneralVX128_3(InstrData& i, InstrDisasm& d) {
  const uint32_t vd = i.VX128_3.VD128l | (i.VX128_3.VD128h << 5);
  const uint32_t vb = i.VX128_3.VB128l | (i.VX128_3.VB128h << 5);
  const uint32_t uimm = i.VX128_3.IMM;
  d.AddRegOperand(InstrRegister::kVMX, vd, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, vb, InstrRegister::kRead);
  d.AddUImmOperand(uimm, 1);
  return d.Finish();
}

}


XEDISASMR(dst,            0x7C0002AC, XDSS)(InstrData& i, InstrDisasm& d) {
  d.Init("dst", "Data Stream Touch",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(dstst,          0x7C0002EC, XDSS)(InstrData& i, InstrDisasm& d) {
  d.Init("dstst", "Data Stream Touch for Store",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(dss,            0x7C00066C, XDSS)(InstrData& i, InstrDisasm& d) {
  d.Init("dss", "Data Stream Stop",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(lvebx,          0x7C00000E, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("lvebx", "Load Vector Element Byte Indexed",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(lvehx,          0x7C00004E, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("lvehx", "Load Vector Element Half Word Indexed",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(lvewx,          0x7C00008E, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("lvewx", "Load Vector Element Word Indexed",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(lvewx128,       VX128_1(4, 131),  VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("lvewx128", "Load Vector128 Element Word Indexed",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(lvsl,           0x7C00000C, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("lvsl", "Load Vector for Shift Left",
         InstrDisasm::kVMX);
  return GeneralX(i, d, false);
}

XEDISASMR(lvsl128,        VX128_1(4, 3),    VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("lvsl128", "Load Vector128 for Shift Left",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, false);
}

XEDISASMR(lvsr,           0x7C00004C, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("lvsr", "Load Vector for Shift Right",
         InstrDisasm::kVMX);
  return GeneralX(i, d, false);
}

XEDISASMR(lvsr128,        VX128_1(4, 67),   VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("lvsr128", "Load Vector128 for Shift Right",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, false);
}

XEDISASMR(lvx,            0x7C0000CE, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("lvx", "Load Vector Indexed",
         InstrDisasm::kVMX);
  return GeneralX(i, d, false);
}

XEDISASMR(lvx128,         VX128_1(4, 195),  VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("lvx128", "Load Vector128 Indexed",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, false);
}

XEDISASMR(lvxl,           0x7C0002CE, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("lvxl", "Load Vector Indexed LRU",
         InstrDisasm::kVMX);
  return GeneralX(i, d, false);
}

XEDISASMR(lvxl128,        VX128_1(4, 707),  VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("lvxl128", "Load Vector128 Left Indexed",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, false);
}

XEDISASMR(stvebx,         0x7C00010E, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("stvebx", "Store Vector Element Byte Indexed",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(stvehx,         0x7C00014E, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("stvehx", "Store Vector Element Half Word Indexed",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(stvewx,         0x7C00018E, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("stvewx", "Store Vector Element Word Indexed",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(stvewx128,      VX128_1(4, 387),  VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("stvewx128", "Store Vector128 Element Word Indexed",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, true);
}

XEDISASMR(stvx,           0x7C0001CE, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("stvx", "Store Vector Indexed",
         InstrDisasm::kVMX);
  return GeneralX(i, d, true);
}

XEDISASMR(stvx128,        VX128_1(4, 451),  VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("stvx128", "Store Vector128 Indexed",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, true);
}

XEDISASMR(stvxl,          0x7C0003CE, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("stvxl", "Store Vector Indexed LRU",
         InstrDisasm::kVMX);
  return GeneralX(i, d, true);
}

XEDISASMR(stvxl128,       VX128_1(4, 963),  VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("stvxl128", "Store Vector128 Indexed LRU",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, true);
}

XEDISASMR(lvlx,           0x7C00040E, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("lvlx", "Load Vector Indexed",
         InstrDisasm::kVMX);
  return GeneralX(i, d, false);
}

XEDISASMR(lvlx128,        VX128_1(4, 1027), VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("lvlx128", "Load Vector128 Left Indexed LRU",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, false);
}

XEDISASMR(lvlxl,          0x7C00060E, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("lvlxl", "Load Vector Indexed LRU",
         InstrDisasm::kVMX);
  return GeneralX(i, d, false);
}

XEDISASMR(lvlxl128,       VX128_1(4, 1539), VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("lvlxl128", "Load Vector128 Indexed LRU",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, false);
}

XEDISASMR(lvrx,           0x7C00044E, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("lvrx", "Load Vector Right Indexed",
         InstrDisasm::kVMX);
  return GeneralX(i, d, false);
}

XEDISASMR(lvrx128,        VX128_1(4, 1091), VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("lvrx128", "Load Vector128 Right Indexed",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, false);
}

XEDISASMR(lvrxl,          0x7C00064E, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("lvrxl", "Load Vector Right Indexed LRU",
         InstrDisasm::kVMX);
  return GeneralX(i, d, false);
}

XEDISASMR(lvrxl128,       VX128_1(4, 1603), VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("lvrxl128", "Load Vector128 Right Indexed LRU",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, false);
}

XEDISASMR(stvlx,          0x7C00050E, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("stvlx", "Store Vector Left Indexed",
         InstrDisasm::kVMX);
  return GeneralX(i, d, true);
}

XEDISASMR(stvlx128,       VX128_1(4, 1283), VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("stvlx128", "Store Vector128 Left Indexed",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, true);
}

XEDISASMR(stvlxl,         0x7C00070E, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("stvlxl", "Store Vector Left Indexed LRU",
         InstrDisasm::kVMX);
  return GeneralX(i, d, true);
}

XEDISASMR(stvlxl128,      VX128_1(4, 1795), VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("stvlxl128", "Store Vector128 Left Indexed LRU",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, true);
}

XEDISASMR(stvrx,          0x7C00054E, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("stvrx", "Store Vector Right Indexed",
         InstrDisasm::kVMX);
  return GeneralX(i, d, true);
}

XEDISASMR(stvrx128,       VX128_1(4, 1347), VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("stvrx128", "Store Vector128 Right Indexed",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, true);
}

XEDISASMR(stvrxl,         0x7C00074E, X   )(InstrData& i, InstrDisasm& d) {
  d.Init("stvrxl", "Store Vector Right Indexed LRU",
         InstrDisasm::kVMX);
  return GeneralX(i, d, true);
}

XEDISASMR(stvrxl128,      VX128_1(4, 1859), VX128_1)(InstrData& i, InstrDisasm& d) {
  d.Init("stvrxl128", "Store Vector128 Right Indexed LRU",
         InstrDisasm::kVMX);
  return GeneralVX128_1(i, d, true);
}

XEDISASMR(mfvscr,         0x10000604, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("mfvscr", "Move from Vector Status and Control Register",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(mtvscr,         0x10000644, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("mtvscr", "Move to Vector Status and Control Register",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vaddcuw,        0x10000180, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vaddcuw", "Vector Add Carryout Unsigned Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vaddfp,         0x1000000A, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vaddfp", "Vector Add Floating Point",
         InstrDisasm::kVMX);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VD, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(vaddfp128,      VX128(5, 16),     VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vaddfp128", "Vector128 Add Floating Point",
         InstrDisasm::kVMX);
  return GeneralVX128(i, d);
}

XEDISASMR(vaddsbs,        0x10000300, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vaddsbs", "Vector Add Signed Byte Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vaddshs,        0x10000340, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vaddshs", "Vector Add Signed Half Word Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vaddsws,        0x10000380, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vaddsws", "Vector Add Signed Word Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vaddubm,        0x10000000, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vaddubm", "Vector Add Unsigned Byte Modulo",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vaddubs,        0x10000200, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vaddubs", "Vector Add Unsigned Byte Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vadduhm,        0x10000040, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vadduhm", "Vector Add Unsigned Half Word Modulo",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vadduhs,        0x10000240, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vadduhs", "Vector Add Unsigned Half Word Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vadduwm,        0x10000080, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vadduwm", "Vector Add Unsigned Word Modulo",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vadduws,        0x10000280, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vadduws", "Vector Add Unsigned Word Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vand,           0x10000404, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vand", "Vector Logical AND",
         InstrDisasm::kVMX);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VD, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(vand128,        VX128(5, 528),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vand128", "Vector128 Logical AND",
         InstrDisasm::kVMX);
  return GeneralVX128(i, d);
}

XEDISASMR(vandc,          0x10000444, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vandc", "Vector Logical AND with Complement",
         InstrDisasm::kVMX);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VD, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(vandc128,       VX128(5, 592),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vandc128", "Vector128 Logical AND with Complement",
         InstrDisasm::kVMX);
  return GeneralVX128(i, d);
}

XEDISASMR(vavgsb,         0x10000502, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vavgsb", "Vector Average Signed Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vavgsh,         0x10000542, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vavgsh", "Vector Average Signed Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vavgsw,         0x10000582, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vavgsw", "Vector Average Signed Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vavgub,         0x10000402, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vavgub", "Vector Average Unsigned Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vavguh,         0x10000442, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vavguh", "Vector Average Unsigned Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vavguw,         0x10000482, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vavguw", "Vector Average Unsigned Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vcfsx,          0x1000034A, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vcfsx", "Vector Convert from Signed Fixed-Point Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vcsxwfp128,     VX128_3(6, 688),  VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vcsxwfp128", "Vector128 Convert From Signed Fixed-Point Word to Floating-Point",
         InstrDisasm::kVMX);
  return GeneralVX128_3(i, d);
}

XEDISASMR(vcfpsxws128,    VX128_3(6, 560),  VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vcfpsxws128", "Vector128 Convert From Floating-Point to Signed Fixed-Point Word Saturate",
         InstrDisasm::kVMX);
  return GeneralVX128_3(i, d);
}

XEDISASMR(vcfux,          0x1000030A, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vcfux", "Vector Convert from Unsigned Fixed-Point Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vcuxwfp128,     VX128_3(6, 752),  VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vcuxwfp128", "Vector128 Convert From Unsigned Fixed-Point Word to Floating-Point",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vcfpuxws128,    VX128_3(6, 624),  VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vcfpuxws128", "Vector128 Convert From Floating-Point to Unsigned Fixed-Point Word Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vcmpbfp,        0x100003C6, VXR )(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpbfp", "Vector Compare Bounds Floating Point",
         InstrDisasm::kVMX | (i.VXR.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpbfp128,     VX128(6, 384),    VX128_R)(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpbfp128", "Vector128 Compare Bounds Floating Point",
         InstrDisasm::kVMX | (i.VX128_R.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpeqfp,       0x100000C6, VXR )(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpeqfp", "Vector Compare Equal-to Floating Point",
         InstrDisasm::kVMX | (i.VXR.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpeqfp128,    VX128(6, 0),      VX128_R)(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpeqfp128", "Vector128 Compare Equal-to Floating Point",
         InstrDisasm::kVMX | (i.VX128_R.Rc ? InstrDisasm::kRc : 0));
  return GeneralVX128(i, d);
}

XEDISASMR(vcmpequb,       0x10000006, VXR )(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpequb", "Vector Compare Equal-to Unsigned Byte",
         InstrDisasm::kVMX | (i.VXR.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpequh,       0x10000046, VXR )(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpequh", "Vector Compare Equal-to Unsigned Half Word",
         InstrDisasm::kVMX | (i.VXR.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpequw,       0x10000086, VXR )(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpequw", "Vector Compare Equal-to Unsigned Word",
         InstrDisasm::kVMX | (i.VXR.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpequw128,    VX128(6, 512),    VX128_R)(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpequw128", "Vector128 Compare Equal-to Unsigned Word",
         InstrDisasm::kVMX | (i.VX128_R.Rc ? InstrDisasm::kRc : 0));
  return GeneralVX128(i, d);
}

XEDISASMR(vcmpgefp,       0x100001C6, VXR )(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpgefp", "Vector Compare Greater-Than-or-Equal-to Floating Point",
         InstrDisasm::kVMX | (i.VXR.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpgefp128,    VX128(6, 128),    VX128_R)(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpgefp128", "Vector128 Compare Greater-Than-or-Equal-to Floating Point",
         InstrDisasm::kVMX | (i.VX128_R.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpgtfp,       0x100002C6, VXR )(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpgtfp", "Vector Compare Greater-Than Floating Point",
         InstrDisasm::kVMX | (i.VXR.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpgtfp128,    VX128(6, 256),    VX128_R)(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpgtfp128", "Vector128 Compare Greater-Than Floating-Point",
         InstrDisasm::kVMX | (i.VX128_R.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpgtsb,       0x10000306, VXR )(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpgtsb", "Vector Compare Greater-Than Signed Byte",
         InstrDisasm::kVMX | (i.VXR.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpgtsh,       0x10000346, VXR )(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpgtsh", "Vector Compare Greater-Than Signed Half Word",
         InstrDisasm::kVMX | (i.VXR.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpgtsw,       0x10000386, VXR )(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpgtsw", "Vector Compare Greater-Than Signed Word",
         InstrDisasm::kVMX | (i.VXR.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpgtub,       0x10000206, VXR )(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpgtub", "Vector Compare Greater-Than Unsigned Byte",
         InstrDisasm::kVMX | (i.VXR.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpgtuh,       0x10000246, VXR )(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpgtuh", "Vector Compare Greater-Than Unsigned Half Word",
         InstrDisasm::kVMX | (i.VXR.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vcmpgtuw,       0x10000286, VXR )(InstrData& i, InstrDisasm& d) {
  d.Init("vcmpgtuw", "Vector Compare Greater-Than Unsigned Word",
         InstrDisasm::kVMX | (i.VXR.Rc ? InstrDisasm::kRc : 0));
  return 1;
}

XEDISASMR(vctsxs,         0x100003CA, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vctsxs", "Vector Convert to Signed Fixed-Point Word Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vctuxs,         0x1000038A, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vctuxs", "Vector Convert to Unsigned Fixed-Point Word Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vexptefp,       0x1000018A, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vexptefp", "Vector 2 Raised to the Exponent Estimate Floating Point",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vexptefp128,    VX128_3(6, 1712), VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vexptefp128", "Vector128 2 Raised to the Exponent Estimate Floating Point",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vlogefp,        0x100001CA, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vlogefp", "Vector Log2 Estimate Floating Point",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vlogefp128,     VX128_3(6, 1776), VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vlogefp128", "Vector128 Log2 Estimate Floating Point",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmaddfp,        0x1000002E, VXA )(InstrData& i, InstrDisasm& d) {
  d.Init("vmaddfp", "Vector Multiply-Add Floating Point",
         InstrDisasm::kVMX);
  return GeneralVXA(i, d);
}

XEDISASMR(vmaddfp128,     VX128(5, 208),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmaddfp128", "Vector128 Multiply Add Floating Point",
         InstrDisasm::kVMX);
  const uint32_t vd = i.VX128.VD128l | (i.VX128.VD128h << 5);
  const uint32_t va = i.VX128.VA128l | (i.VX128.VA128h << 5) |
                                       (i.VX128.VA128H << 6);
  const uint32_t vb = i.VX128.VB128l | (i.VX128.VB128h << 5);
  d.AddRegOperand(InstrRegister::kVMX, vd, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, va, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, vb, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, vd, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(vmaddcfp128,    VX128(5, 272),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmaddcfp128", "Vector128 Multiply Add Floating Point",
         InstrDisasm::kVMX);
  const uint32_t vd = i.VX128.VD128l | (i.VX128.VD128h << 5);
  const uint32_t va = i.VX128.VA128l | (i.VX128.VA128h << 5) |
                                       (i.VX128.VA128H << 6);
  const uint32_t vb = i.VX128.VB128l | (i.VX128.VB128h << 5);
  d.AddRegOperand(InstrRegister::kVMX, vd, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, va, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, vd, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, vb, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(vmaxfp,         0x1000040A, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmaxfp", "Vector Maximum Floating Point",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmaxfp128,      VX128(6, 640),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmaxfp128", "Vector128 Maximum Floating Point",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmaxsb,         0x10000102, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmaxsb", "Vector Maximum Signed Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmaxsh,         0x10000142, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmaxsh", "Vector Maximum Signed Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmaxsw,         0x10000182, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmaxsw", "Vector Maximum Signed Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmaxub,         0x10000002, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmaxub", "Vector Maximum Unsigned Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmaxuh,         0x10000042, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmaxuh", "Vector Maximum Unsigned Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmaxuw,         0x10000082, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmaxuw", "Vector Maximum Unsigned Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmhaddshs,      0x10000020, VXA )(InstrData& i, InstrDisasm& d) {
  d.Init("vmhaddshs", "Vector Multiply-High and Add Signed Signed Half Word Saturate",
         InstrDisasm::kVMX);
  return GeneralVXA(i, d);
}

XEDISASMR(vmhraddshs,     0x10000021, VXA )(InstrData& i, InstrDisasm& d) {
  d.Init("vmhraddshs", "Vector Multiply-High Round and Add Signed Signed Half Word Saturate",
         InstrDisasm::kVMX);
  return GeneralVXA(i, d);
}

XEDISASMR(vminfp,         0x1000044A, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vminfp", "Vector Minimum Floating Point",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vminfp128,      VX128(6, 704),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vminfp128", "Vector128 Minimum Floating Point",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vminsb,         0x10000302, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vminsb", "Vector Minimum Signed Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vminsh,         0x10000342, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vminsh", "Vector Minimum Signed Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vminsw,         0x10000382, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vminsw", "Vector Minimum Signed Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vminub,         0x10000202, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vminub", "Vector Minimum Unsigned Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vminuh,         0x10000242, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vminuh", "Vector Minimum Unsigned Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vminuw,         0x10000282, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vminuw", "Vector Minimum Unsigned Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmladduhm,      0x10000022, VXA )(InstrData& i, InstrDisasm& d) {
  d.Init("vmladduhm", "Vector Multiply-Low and Add Unsigned Half Word Modulo",
         InstrDisasm::kVMX);
  return GeneralVXA(i, d);
}

XEDISASMR(vmrghb,         0x1000000C, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmrghb", "Vector Merge High Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmrghh,         0x1000004C, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmrghh", "Vector Merge High Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmrghw,         0x1000008C, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmrghw", "Vector Merge High Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmrghw128,      VX128(6, 768),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmrghw128", "Vector128 Merge High Word",
         InstrDisasm::kVMX);
  return GeneralVX128(i, d);
}

XEDISASMR(vmrglb,         0x1000010C, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmrglb", "Vector Merge Low Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmrglh,         0x1000014C, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmrglh", "Vector Merge Low Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmrglw,         0x1000018C, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmrglw", "Vector Merge Low Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmrglw128,      VX128(6, 832),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmrglw128", "Vector128 Merge Low Word",
         InstrDisasm::kVMX);
  return GeneralVX128(i, d);
}

XEDISASMR(vmsummbm,       0x10000025, VXA )(InstrData& i, InstrDisasm& d) {
  d.Init("vmsummbm", "Vector Multiply-Sum Mixed-Sign Byte Modulo",
         InstrDisasm::kVMX);
  return GeneralVXA(i, d);
}

XEDISASMR(vmsumshm,       0x10000028, VXA )(InstrData& i, InstrDisasm& d) {
  d.Init("vmsumshm", "Vector Multiply-Sum Signed Half Word Modulo",
         InstrDisasm::kVMX);
  return GeneralVXA(i, d);
}

XEDISASMR(vmsumshs,       0x10000029, VXA )(InstrData& i, InstrDisasm& d) {
  d.Init("vmsumshs", "Vector Multiply-Sum Signed Half Word Saturate",
         InstrDisasm::kVMX);
  return GeneralVXA(i, d);
}

XEDISASMR(vmsumubm,       0x10000024, VXA )(InstrData& i, InstrDisasm& d) {
  d.Init("vmsumubm", "Vector Multiply-Sum Unsigned Byte Modulo",
         InstrDisasm::kVMX);
  return GeneralVXA(i, d);
}

XEDISASMR(vmsumuhm,       0x10000026, VXA )(InstrData& i, InstrDisasm& d) {
  d.Init("vmsumuhm", "Vector Multiply-Sum Unsigned Half Word Modulo",
         InstrDisasm::kVMX);
  return GeneralVXA(i, d);
}

XEDISASMR(vmsumuhs,       0x10000027, VXA )(InstrData& i, InstrDisasm& d) {
  d.Init("vmsumuhs", "Vector Multiply-Sum Unsigned Half Word Saturate",
         InstrDisasm::kVMX);
  return GeneralVXA(i, d);
}

XEDISASMR(vmsum3fp128,    VX128(5, 400),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmsum3fp128", "Vector128 Multiply Sum 3-way Floating Point",
         InstrDisasm::kVMX);
  return GeneralVX128(i, d);
}

XEDISASMR(vmsum4fp128,    VX128(5, 464),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmsum4fp128", "Vector128 Multiply Sum 4-way Floating-Point",
         InstrDisasm::kVMX);
  return GeneralVX128(i, d);
}

XEDISASMR(vmulesb,        0x10000308, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmulesb", "Vector Multiply Even Signed Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmulesh,        0x10000348, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmulesh", "Vector Multiply Even Signed Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmuleub,        0x10000208, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmuleub", "Vector Multiply Even Unsigned Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmuleuh,        0x10000248, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmuleuh", "Vector Multiply Even Unsigned Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmulosb,        0x10000108, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmulosb", "Vector Multiply Odd Signed Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmulosh,        0x10000148, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmulosh", "Vector Multiply Odd Signed Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmuloub,        0x10000008, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmuloub", "Vector Multiply Odd Unsigned Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmulouh,        0x10000048, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmulouh", "Vector Multiply Odd Unsigned Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vmulfp128,      VX128(5, 144),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vmulfp128", "Vector128 Multiply Floating-Point",
         InstrDisasm::kVMX);
  return GeneralVX128(i, d);
}

XEDISASMR(vnmsubfp,       0x1000002F, VXA )(InstrData& i, InstrDisasm& d) {
  d.Init("vnmsubfp", "Vector Negative Multiply-Subtract Floating Point",
         InstrDisasm::kVMX);
  return GeneralVXA(i, d);
}

XEDISASMR(vnmsubfp128,    VX128(5, 336),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vnmsubfp128", "Vector128 Negative Multiply-Subtract Floating Point",
         InstrDisasm::kVMX);
  return GeneralVX128(i, d);
}

XEDISASMR(vnor,           0x10000504, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vnor", "Vector Logical NOR",
         InstrDisasm::kVMX);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VD, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(vnor128,        VX128(5, 656),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vnor128", "Vector128 Logical NOR",
         InstrDisasm::kVMX);
  return GeneralVX128(i, d);
}

XEDISASMR(vor,            0x10000484, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vor", "Vector Logical OR",
         InstrDisasm::kVMX);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VD, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(vor128,         VX128(5, 720),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vor128", "Vector128 Logical OR",
         InstrDisasm::kVMX);
  return GeneralVX128(i, d);
}

XEDISASMR(vperm,          0x1000002B, VXA )(InstrData& i, InstrDisasm& d) {
  d.Init("vperm", "Vector Permute",
         InstrDisasm::kVMX);
  return GeneralVXA(i, d);
}

XEDISASMR(vperm128,       VX128_2(5, 0),    VX128_2)(InstrData& i, InstrDisasm& d) {
  d.Init("vperm128", "Vector128 Permute",
         InstrDisasm::kVMX);
  const uint32_t vd = i.VX128_2.VD128l | (i.VX128_2.VD128h << 5);
  const uint32_t va = i.VX128_2.VA128l | (i.VX128_2.VA128h << 5) |
                                         (i.VX128_2.VA128H << 6);
  const uint32_t vb = i.VX128_2.VB128l | (i.VX128_2.VB128h << 5);
  d.AddRegOperand(InstrRegister::kVMX, vd, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, va, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, vb, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, i.VX128_2.VC, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(vpermwi128,     VX128_P(6, 528),  VX128_P)(InstrData& i, InstrDisasm& d) {
  d.Init("vpermwi128", "Vector128 Permutate Word Immediate",
         InstrDisasm::kVMX);
  const uint32_t vd = i.VX128_P.VD128l | (i.VX128_P.VD128h << 5);
  const uint32_t vb = i.VX128_P.VB128l | (i.VX128_P.VB128h << 5);
  d.AddRegOperand(InstrRegister::kVMX, vd, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, vb, InstrRegister::kRead);
  d.AddUImmOperand(i.VX128_P.PERMl | (i.VX128_P.PERMh << 5), 1);
  return d.Finish();
}

XEDISASMR(vpkpx,          0x1000030E, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkpx", "Vector Pack Pixel",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkshss,        0x1000018E, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkshss", "Vector Pack Signed Half Word Signed Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkshss128,     VX128(5, 512),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkshss128", "Vector128 Pack Signed Half Word Signed Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkswss,        0x100001CE, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkswss", "Vector Pack Signed Word Signed Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkswss128,     VX128(5, 640),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkswss128", "Vector128 Pack Signed Word Signed Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkswus,        0x1000014E, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkswus", "Vector Pack Signed Word Unsigned Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkswus128,     VX128(5, 704),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkswus128", "Vector128 Pack Signed Word Unsigned Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkuhum,        0x1000000E, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkuhum", "Vector Pack Unsigned Half Word Unsigned Modulo",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkuhum128,     VX128(5, 768),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkuhum128", "Vector128 Pack Unsigned Half Word Unsigned Modulo",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkuhus,        0x1000008E, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkuhus", "Vector Pack Unsigned Half Word Unsigned Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkuhus128,     VX128(5, 832),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkuhus128", "Vector128 Pack Unsigned Half Word Unsigned Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkshus,        0x1000010E, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkshus", "Vector Pack Signed Half Word Unsigned Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkshus128,     VX128(5, 576),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkshus128", "Vector128 Pack Signed Half Word Unsigned Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkuwum,        0x1000004E, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkuwum", "Vector Pack Unsigned Word Unsigned Modulo",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkuwum128,     VX128(5, 896),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkuwum128", "Vector128 Pack Unsigned Word Unsigned Modulo",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkuwus,        0x100000CE, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkuwus", "Vector Pack Unsigned Word Unsigned Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkuwus128,     VX128(5, 960),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vpkuwus128", "Vector128 Pack Unsigned Word Unsigned Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vpkd3d128,      VX128_4(6, 1552), VX128_4)(InstrData& i, InstrDisasm& d) {
  d.Init("vpkd3d128", "Vector128 Pack D3Dtype, Rotate Left Immediate and Mask Insert",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrefp,          0x1000010A, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vrefp", "Vector Reciprocal Estimate Floating Point",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrefp128,       VX128_3(6, 1584), VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vrefp128", "Vector128 Reciprocal Estimate Floating Point",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrfim,          0x100002CA, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vrfim", "Vector Round to Floating-Point Integer toward -Infinity",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrfim128,       VX128_3(6, 816),  VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vrfim128", "Vector128 Round to Floating-Point Integer toward -Infinity",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrfin,          0x1000020A, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vrfin", "Vector Round to Floating-Point Integer Nearest",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrfin128,       VX128_3(6, 880),  VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vrfin128", "Vector128 Round to Floating-Point Integer Nearest",
         InstrDisasm::kVMX);
  const uint32_t vd = i.VX128_3.VD128l | (i.VX128_3.VD128h << 5);
  const uint32_t vb = i.VX128_3.VB128l | (i.VX128_3.VB128h << 5);
  d.AddRegOperand(InstrRegister::kVMX, vd, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, vb, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(vrfip,          0x1000028A, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vrfip", "Vector Round to Floating-Point Integer toward +Infinity",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrfip128,       VX128_3(6, 944),  VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vrfip128", "Vector128 Round to Floating-Point Integer toward +Infinity",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrfiz,          0x1000024A, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vrfiz", "Vector Round to Floating-Point Integer toward Zero",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrfiz128,       VX128_3(6, 1008), VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vrfiz128", "Vector128 Round to Floating-Point Integer toward Zero",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrlb,           0x10000004, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vrlb", "Vector Rotate Left Integer Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrlh,           0x10000044, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vrlh", "Vector Rotate Left Integer Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrlw,           0x10000084, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vrlw", "Vector Rotate Left Integer Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrlw128,        VX128(6, 80),     VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vrlw128", "Vector128 Rotate Left Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrlimi128,      VX128_4(6, 1808), VX128_4)(InstrData& i, InstrDisasm& d) {
  d.Init("vrlimi128", "Vector128 Rotate Left Immediate and Mask Insert",
         InstrDisasm::kVMX);
  const uint32_t vd = i.VX128_4.VD128l | (i.VX128_4.VD128h << 5);
  const uint32_t vb = i.VX128_4.VB128l | (i.VX128_4.VB128h << 5);
  d.AddRegOperand(InstrRegister::kVMX, vd, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, vb, InstrRegister::kRead);
  d.AddUImmOperand(i.VX128_4.IMM, 1);
  d.AddUImmOperand(i.VX128_4.z, 1);
  return d.Finish();
}

XEDISASMR(vrsqrtefp,      0x1000014A, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vrsqrtefp", "Vector Reciprocal Square Root Estimate Floating Point",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vrsqrtefp128,   VX128_3(6, 1648), VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vrsqrtefp128", "Vector128 Reciprocal Square Root Estimate Floating Point",
         InstrDisasm::kVMX);
  const uint32_t vd = i.VX128_3.VD128l | (i.VX128_3.VD128h << 5);
  const uint32_t vb = i.VX128_3.VB128l | (i.VX128_3.VB128h << 5);
  d.AddRegOperand(InstrRegister::kVMX, vd, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, vb, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(vsel,           0x1000002A, VXA )(InstrData& i, InstrDisasm& d) {
  d.Init("vsel", "Vector Conditional Select",
         InstrDisasm::kVMX);
  return GeneralVXA(i, d);
}

XEDISASMR(vsel128,        VX128(5, 848),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsel128", "Vector128 Conditional Select",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsl,            0x100001C4, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsl", "Vector Shift Left",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vslb,           0x10000104, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vslb", "Vector Shift Left Integer Byte",
         InstrDisasm::kVMX);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VD, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(vsldoi,         0x1000002C, VXA )(InstrData& i, InstrDisasm& d) {
  d.Init("vsldoi", "Vector Shift Left Double by Octet Immediate",
         InstrDisasm::kVMX);
  return GeneralVXA(i, d);
}

XEDISASMR(vsldoi128,      VX128_5(4, 16),   VX128_5)(InstrData& i, InstrDisasm& d) {
  d.Init("vsldoi128", "Vector128 Shift Left Double by Octet Immediate",
         InstrDisasm::kVMX);
  const uint32_t vd = i.VX128_5.VD128l | (i.VX128_5.VD128h << 5);
  const uint32_t va = i.VX128_5.VA128l | (i.VX128_5.VA128h << 5);
  const uint32_t vb = i.VX128_5.VB128l | (i.VX128_5.VB128h << 5);
  const uint32_t sh = i.VX128_5.SH;
  d.AddRegOperand(InstrRegister::kVMX, vd, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, va, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, vb, InstrRegister::kRead);
  d.AddUImmOperand(sh, 1);
  return d.Finish();
}

XEDISASMR(vslh,           0x10000144, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vslh", "Vector Shift Left Integer Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vslo,           0x1000040C, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vslo", "Vector Shift Left by Octet",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vslo128,        VX128(5, 912),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vslo128", "Vector128 Shift Left Octet",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vslw,           0x10000184, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vslw", "Vector Shift Left Integer Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vslw128,        VX128(6, 208),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vslw128", "Vector128 Shift Left Integer Word",
         InstrDisasm::kVMX);
  return GeneralVX128(i, d);
}

XEDISASMR(vspltb,         0x1000020C, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vspltb", "Vector Splat Byte",
         InstrDisasm::kVMX);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VD, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VB, InstrRegister::kRead);
  d.AddUImmOperand(i.VX.VA & 0xF, 1);
  return d.Finish();
}

XEDISASMR(vsplth,         0x1000024C, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsplth", "Vector Splat Half Word",
         InstrDisasm::kVMX);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VD, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VB, InstrRegister::kRead);
  d.AddUImmOperand(i.VX.VA & 0x7, 1);
  return d.Finish();
}

XEDISASMR(vspltisb,       0x1000030C, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vspltisb", "Vector Splat Immediate Signed Byte",
         InstrDisasm::kVMX);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VD, InstrRegister::kWrite);
  // 5bit -> 8bit sign extend
  uint64_t v = (i.VX.VA & 0x10) ? (i.VX.VA | 0xFFFFFFFFFFFFFFF0) : i.VX.VA;
  d.AddSImmOperand(v, 1);
  return d.Finish();
}

XEDISASMR(vspltish,       0x1000034C, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vspltish", "Vector Splat Immediate Signed Half Word",
         InstrDisasm::kVMX);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VD, InstrRegister::kWrite);
  // 5bit -> 16bit sign extend
  uint64_t v = (i.VX.VA & 0x10) ? (i.VX.VA | 0xFFFFFFFFFFFFFFF0) : i.VX.VA;
  d.AddSImmOperand(v, 1);
  return d.Finish();
}

XEDISASMR(vspltisw,       0x1000038C, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vspltisw", "Vector Splat Immediate Signed Word",
         InstrDisasm::kVMX);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VD, InstrRegister::kWrite);
  // 5bit -> 32bit sign extend
  uint64_t v = (i.VX.VA & 0x10) ? (i.VX.VA | 0xFFFFFFFFFFFFFFF0) : i.VX.VA;
  d.AddSImmOperand(v, 1);
  return d.Finish();
}

XEDISASMR(vspltisw128,    VX128_3(6, 1904), VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vspltisw128", "Vector128 Splat Immediate Signed Word",
         InstrDisasm::kVMX);
  return GeneralVX128_3(i, d);
}

XEDISASMR(vspltw,         0x1000028C, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vspltw", "Vector Splat Word",
         InstrDisasm::kVMX);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VD, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VB, InstrRegister::kRead);
  d.AddUImmOperand(i.VX.VA, 1);
  return d.Finish();
}

XEDISASMR(vspltw128,      VX128_3(6, 1840), VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vspltw128", " Vector128 Splat Word",
         InstrDisasm::kVMX);
  return GeneralVX128_3(i, d);
}

XEDISASMR(vsr,            0x100002C4, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsr", "Vector Shift Right",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsrab,          0x10000304, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsrab", "Vector Shift Right Algebraic Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsrah,          0x10000344, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsrah", "Vector Shift Right Algebraic Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsraw,          0x10000384, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsraw", "Vector Shift Right Algebraic Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsraw128,       VX128(6, 336),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsraw128", "Vector128 Shift Right Arithmetic Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsrb,           0x10000204, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsrb", "Vector Shift Right Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsrh,           0x10000244, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsrh", "Vector Shift Right Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsro,           0x1000044C, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsro", "Vector Shift Right Octet",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsro128,        VX128(5, 976),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsro128", "Vector128 Shift Right Octet",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsrw,           0x10000284, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsrw", "Vector Shift Right Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsrw128,        VX128(6, 464),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsrw128", "Vector128 Shift Right Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsubcuw,        0x10000580, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsubcuw", "Vector Subtract Carryout Unsigned Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsubfp,         0x1000004A, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsubfp", "Vector Subtract Floating Point",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsubfp128,      VX128(5, 80),     VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsubfp128", "Vector128 Subtract Floating Point",
         InstrDisasm::kVMX);
  return GeneralVX128(i, d);
}

XEDISASMR(vsubsbs,        0x10000700, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsubsbs", "Vector Subtract Signed Byte Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsubshs,        0x10000740, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsubshs", "Vector Subtract Signed Half Word Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsubsws,        0x10000780, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsubsws", "Vector Subtract Signed Word Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsububm,        0x10000400, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsububm", "Vector Subtract Unsigned Byte Modulo",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsububs,        0x10000600, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsububs", "Vector Subtract Unsigned Byte Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsubuhm,        0x10000440, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsubuhm", "Vector Subtract Unsigned Half Word Modulo",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsubuhs,        0x10000640, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsubuhs", "Vector Subtract Unsigned Half Word Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsubuwm,        0x10000480, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsubuwm", "Vector Subtract Unsigned Word Modulo",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsubuws,        0x10000680, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsubuws", "Vector Subtract Unsigned Word Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsumsws,        0x10000788, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsumsws", "Vector Sum Across Signed Word Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsum2sws,       0x10000688, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsum2sws", "Vector Sum Across Partial (1/2) Signed Word Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsum4sbs,       0x10000708, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsum4sbs", "Vector Sum Across Partial (1/4) Signed Byte Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsum4shs,       0x10000648, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsum4shs", "Vector Sum Across Partial (1/4) Signed Half Word Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vsum4ubs,       0x10000608, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vsum4ubs", "Vector Sum Across Partial (1/4) Unsigned Byte Saturate",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vupkhpx,        0x1000034E, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vupkhpx", "Vector Unpack High Pixel",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vupkhsb,        0x1000020E, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vupkhsb", "Vector Unpack High Signed Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vupkhsb128,     VX128(6, 896),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vupkhsb128", "Vector128 Unpack High Signed Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vupkhsh,        0x1000024E, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vupkhsh", "Vector Unpack High Signed Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vupklpx,        0x100003CE, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vupklpx", "Vector Unpack Low Pixel",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vupklsb,        0x1000028E, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vupklsb", "Vector Unpack Low Signed Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vupklsb128,     VX128(6, 960),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vupklsb128", "Vector128 Unpack Low Signed Byte",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vupklsh,        0x100002CE, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vupklsh", "Vector Unpack Low Signed Half Word",
         InstrDisasm::kVMX);
  return 1;
}

XEDISASMR(vupkd3d128,     VX128_3(6, 2032), VX128_3)(InstrData& i, InstrDisasm& d) {
  d.Init("vupkd3d128", "Vector128 Unpack D3Dtype",
         InstrDisasm::kVMX);
  const uint32_t vd = i.VX128_3.VD128l | (i.VX128_3.VD128h << 5);
  const uint32_t vb = i.VX128_3.VB128l | (i.VX128_3.VB128h << 5);
  d.AddRegOperand(InstrRegister::kVMX, vd, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, vb, InstrRegister::kRead);
  d.AddUImmOperand(i.VX128_3.IMM, 1);
  return d.Finish();
}

XEDISASMR(vxor,           0x100004C4, VX  )(InstrData& i, InstrDisasm& d) {
  d.Init("vxor", "Vector Logical XOR",
         InstrDisasm::kVMX);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VD, InstrRegister::kWrite);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VA, InstrRegister::kRead);
  d.AddRegOperand(InstrRegister::kVMX, i.VX.VB, InstrRegister::kRead);
  return d.Finish();
}

XEDISASMR(vxor128,        VX128(5, 784),    VX128  )(InstrData& i, InstrDisasm& d) {
  d.Init("vxor128", "Vector128 Logical XOR",
         InstrDisasm::kVMX);
  return GeneralVX128(i, d);
}


void RegisterDisasmCategoryAltivec() {
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
  XEREGISTERINSTR(vcmpequb,       0x10000006);
  XEREGISTERINSTR(vcmpequh,       0x10000046);
  XEREGISTERINSTR(vcmpequw,       0x10000086);
  XEREGISTERINSTR(vcmpequw128,    VX128(6, 512));
  XEREGISTERINSTR(vcmpgefp,       0x100001C6);
  XEREGISTERINSTR(vcmpgefp128,    VX128(6, 128));
  XEREGISTERINSTR(vcmpgtfp,       0x100002C6);
  XEREGISTERINSTR(vcmpgtfp128,    VX128(6, 256));
  XEREGISTERINSTR(vcmpgtsb,       0x10000306);
  XEREGISTERINSTR(vcmpgtsh,       0x10000346);
  XEREGISTERINSTR(vcmpgtsw,       0x10000386);
  XEREGISTERINSTR(vcmpgtub,       0x10000206);
  XEREGISTERINSTR(vcmpgtuh,       0x10000246);
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


}  // namespace ppc
}  // namespace frontend
}  // namespace alloy
