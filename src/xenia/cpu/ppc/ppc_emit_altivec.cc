/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/ppc/ppc_emit-private.h"

#include "xenia/base/assert.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/ppc/ppc_hir_builder.h"

#include <cmath>

namespace xe {
namespace cpu {
namespace ppc {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::Value;

Value* CalculateEA_0(PPCHIRBuilder& f, uint32_t ra, uint32_t rb);

#define SHUFPS_SWAP_DWORDS 0x1B

// Most of this file comes from:
// http://biallas.net/doc/vmx128/vmx128.txt
// https://github.com/kakaroto/ps3ida/blob/master/plugins/PPCAltivec/src/main.cpp
// https://sannybuilder.com/forums/viewtopic.php?id=190

#define OP(x) ((((uint32_t)(x)) & 0x3f) << 26)
#define VX128(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x3d0))
#define VX128_1(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x7f3))
#define VX128_2(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x210))
#define VX128_3(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x7f0))
#define VX128_4(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x730))
#define VX128_5(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x10))
#define VX128_P(op, xop) (OP(op) | (((uint32_t)(xop)) & 0x630))

#define VX128_VD128 (i.VX128.VD128l | (i.VX128.VD128h << 5))
#define VX128_VA128 \
  (i.VX128.VA128l | (i.VX128.VA128h << 5) | (i.VX128.VA128H << 6))
#define VX128_VB128 (i.VX128.VB128l | (i.VX128.VB128h << 5))
#define VX128_1_VD128 (i.VX128_1.VD128l | (i.VX128_1.VD128h << 5))
#define VX128_2_VD128 (i.VX128_2.VD128l | (i.VX128_2.VD128h << 5))
#define VX128_2_VA128 \
  (i.VX128_2.VA128l | (i.VX128_2.VA128h << 5) | (i.VX128_2.VA128H << 6))
#define VX128_2_VB128 (i.VX128_2.VB128l | (i.VX128_2.VB128h << 5))
#define VX128_2_VC (i.VX128_2.VC)
#define VX128_3_VD128 (i.VX128_3.VD128l | (i.VX128_3.VD128h << 5))
#define VX128_3_VB128 (i.VX128_3.VB128l | (i.VX128_3.VB128h << 5))
#define VX128_3_IMM (i.VX128_3.IMM)
#define VX128_5_VD128 (i.VX128_5.VD128l | (i.VX128_5.VD128h << 5))
#define VX128_5_VA128 \
  (i.VX128_5.VA128l | (i.VX128_5.VA128h << 5)) | (i.VX128_5.VA128H << 6)
#define VX128_5_VB128 (i.VX128_5.VB128l | (i.VX128_5.VB128h << 5))
#define VX128_5_SH (i.VX128_5.SH)
#define VX128_R_VD128 (i.VX128_R.VD128l | (i.VX128_R.VD128h << 5))
#define VX128_R_VA128 \
  (i.VX128_R.VA128l | (i.VX128_R.VA128h << 5) | (i.VX128_R.VA128H << 6))
#define VX128_R_VB128 (i.VX128_R.VB128l | (i.VX128_R.VB128h << 5))

unsigned int xerotl(unsigned int value, unsigned int shift) {
  assert_true(shift < 32);
  return shift == 0 ? value : ((value << shift) | (value >> (32 - shift)));
}

int InstrEmit_lvebx(PPCHIRBuilder& f, const InstrData& i) {
  // Same as lvx.
  Value* ea =
      f.And(CalculateEA_0(f, i.X.RA, i.X.RB), f.LoadConstantUint64(~0xFull));
  f.StoreVR(i.X.RT, f.ByteSwap(f.Load(ea, VEC128_TYPE)));
  return 0;
}

int InstrEmit_lvehx(PPCHIRBuilder& f, const InstrData& i) {
  // Same as lvx.
  Value* ea =
      f.And(CalculateEA_0(f, i.X.RA, i.X.RB), f.LoadConstantUint64(~0xFull));
  f.StoreVR(i.X.RT, f.ByteSwap(f.Load(ea, VEC128_TYPE)));
  return 0;
}

int InstrEmit_lvewx_(PPCHIRBuilder& f, const InstrData& i, uint32_t vd,
                     uint32_t ra, uint32_t rb) {
  // Same as lvx.
  Value* ea = f.And(CalculateEA_0(f, ra, rb), f.LoadConstantUint64(~0xFull));
  f.StoreVR(vd, f.ByteSwap(f.Load(ea, VEC128_TYPE)));
  return 0;
}
int InstrEmit_lvewx(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvewx_(f, i, i.X.RT, i.X.RA, i.X.RB);
}
int InstrEmit_lvewx128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvewx_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}

int InstrEmit_lvsl_(PPCHIRBuilder& f, const InstrData& i, uint32_t vd,
                    uint32_t ra, uint32_t rb) {
  Value* ea = CalculateEA_0(f, ra, rb);
  Value* sh = f.Truncate(f.And(ea, f.LoadConstantInt64(0xF)), INT8_TYPE);
  Value* v = f.LoadVectorShl(sh);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_lvsl(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvsl_(f, i, i.X.RT, i.X.RA, i.X.RB);
}
int InstrEmit_lvsl128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvsl_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}

int InstrEmit_lvsr_(PPCHIRBuilder& f, const InstrData& i, uint32_t vd,
                    uint32_t ra, uint32_t rb) {
  Value* ea = CalculateEA_0(f, ra, rb);
  Value* sh = f.Truncate(f.And(ea, f.LoadConstantInt64(0xF)), INT8_TYPE);
  Value* v = f.LoadVectorShr(sh);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_lvsr(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvsr_(f, i, i.X.RT, i.X.RA, i.X.RB);
}
int InstrEmit_lvsr128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvsr_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}

int InstrEmit_lvx_(PPCHIRBuilder& f, const InstrData& i, uint32_t vd,
                   uint32_t ra, uint32_t rb) {
  Value* ea = f.And(CalculateEA_0(f, ra, rb), f.LoadConstantInt64(~0xFull));
  f.StoreVR(vd, f.ByteSwap(f.Load(ea, VEC128_TYPE)));
  return 0;
}
int InstrEmit_lvx(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvx_(f, i, i.X.RT, i.X.RA, i.X.RB);
}
int InstrEmit_lvx128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvx_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}
int InstrEmit_lvxl(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvx(f, i);
}
int InstrEmit_lvxl128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvx128(f, i);
}

int InstrEmit_stvebx(PPCHIRBuilder& f, const InstrData& i) {
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  Value* el = f.And(f.Truncate(ea, INT8_TYPE), f.LoadConstantUint8(0xF));
  Value* v = f.Extract(f.LoadVR(i.X.RT), el, INT8_TYPE);
  f.Store(ea, v);
  return 0;
}

int InstrEmit_stvehx(PPCHIRBuilder& f, const InstrData& i) {
  Value* ea = CalculateEA_0(f, i.X.RA, i.X.RB);
  ea = f.And(ea, f.LoadConstantUint64(~0x1ull));
  Value* el =
      f.Shr(f.And(f.Truncate(ea, INT8_TYPE), f.LoadConstantUint8(0xF)), 1);
  Value* v = f.Extract(f.LoadVR(i.X.RT), el, INT16_TYPE);
  f.Store(ea, f.ByteSwap(v));
  return 0;
}

int InstrEmit_stvewx_(PPCHIRBuilder& f, const InstrData& i, uint32_t vd,
                      uint32_t ra, uint32_t rb) {
  Value* ea = CalculateEA_0(f, ra, rb);
  ea = f.And(ea, f.LoadConstantUint64(~0x3ull));
  Value* el =
      f.Shr(f.And(f.Truncate(ea, INT8_TYPE), f.LoadConstantUint8(0xF)), 2);
  Value* v = f.Extract(f.LoadVR(vd), el, INT32_TYPE);
  f.Store(ea, f.ByteSwap(v));
  return 0;
}
int InstrEmit_stvewx(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_stvewx_(f, i, i.X.RT, i.X.RA, i.X.RB);
}
int InstrEmit_stvewx128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_stvewx_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}

int InstrEmit_stvx_(PPCHIRBuilder& f, const InstrData& i, uint32_t vd,
                    uint32_t ra, uint32_t rb) {
  Value* ea = f.And(CalculateEA_0(f, ra, rb), f.LoadConstantUint64(~0xFull));
  f.Store(ea, f.ByteSwap(f.LoadVR(vd)));
  return 0;
}
int InstrEmit_stvx(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_stvx_(f, i, i.X.RT, i.X.RA, i.X.RB);
}
int InstrEmit_stvx128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_stvx_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}
int InstrEmit_stvxl(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_stvx(f, i);
}
int InstrEmit_stvxl128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_stvx128(f, i);
}

// The lvlx/lvrx/etc instructions are in Cell docs only:
// https://www-01.ibm.com/chips/techlib/techlib.nsf/techdocs/C40E4C6133B31EE8872570B500791108/$file/vector_simd_pem_v_2.07c_26Oct2006_cell.pdf
int InstrEmit_lvlx_(PPCHIRBuilder& f, const InstrData& i, uint32_t vd,
                    uint32_t ra, uint32_t rb) {
  Value* ea = CalculateEA_0(f, ra, rb);

  Value* val = f.LoadVectorLeft(ea);
  f.StoreVR(vd, val);
  return 0;
}
int InstrEmit_lvlx(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvlx_(f, i, i.X.RT, i.X.RA, i.X.RB);
}
int InstrEmit_lvlx128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvlx_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}
int InstrEmit_lvlxl(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvlx(f, i);
}
int InstrEmit_lvlxl128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvlx128(f, i);
}

int InstrEmit_lvrx_(PPCHIRBuilder& f, const InstrData& i, uint32_t vd,
                    uint32_t ra, uint32_t rb) {
  // NOTE: if eb == 0 (so 16b aligned) then no data is loaded. This is important
  // as often times memcpy's will use this to handle the remaining <=16b of a
  // buffer, which sometimes may be nothing and hang off the end of the valid
  // page area. We still need to zero the resulting register, though.
  Value* ea = CalculateEA_0(f, ra, rb);

  Value* val = f.LoadVectorRight(ea);
  f.StoreVR(vd, val);
  return 0;
}
int InstrEmit_lvrx(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvrx_(f, i, i.X.RT, i.X.RA, i.X.RB);
}
int InstrEmit_lvrx128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvrx_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}
int InstrEmit_lvrxl(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvrx(f, i);
}
int InstrEmit_lvrxl128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_lvrx128(f, i);
}

int InstrEmit_stvlx_(PPCHIRBuilder& f, const InstrData& i, uint32_t vd,
                     uint32_t ra, uint32_t rb) {
  // NOTE: if eb == 0 (so 16b aligned) this equals new_value
  //       we could optimize this to prevent the other load/mask, in that case.

  Value* ea = CalculateEA_0(f, ra, rb);

  Value* vdr = f.LoadVR(vd);
  f.StoreVectorLeft(ea, vdr);
  return 0;
}
int InstrEmit_stvlx(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_stvlx_(f, i, i.X.RT, i.X.RA, i.X.RB);
}
int InstrEmit_stvlx128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_stvlx_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}
int InstrEmit_stvlxl(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_stvlx(f, i);
}
int InstrEmit_stvlxl128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_stvlx128(f, i);
}

int InstrEmit_stvrx_(PPCHIRBuilder& f, const InstrData& i, uint32_t vd,
                     uint32_t ra, uint32_t rb) {
  // NOTE: if eb == 0 (so 16b aligned) then no data is loaded. This is important
  // as often times memcpy's will use this to handle the remaining <=16b of a
  // buffer, which sometimes may be nothing and hang off the end of the valid
  // page area.
  Value* ea = CalculateEA_0(f, ra, rb);

  Value* vdr = f.LoadVR(vd);
  f.StoreVectorRight(ea, vdr);
  return 0;
}
int InstrEmit_stvrx(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_stvrx_(f, i, i.X.RT, i.X.RA, i.X.RB);
}
int InstrEmit_stvrx128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_stvrx_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}
int InstrEmit_stvrxl(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_stvrx(f, i);
}
int InstrEmit_stvrxl128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_stvrx128(f, i);
}

int InstrEmit_mfvscr(PPCHIRBuilder& f, const InstrData& i) {
  // is this the right format?

  f.StoreVR(i.VX128_1.RB,
            f.LoadContext(offsetof(PPCContext, vscr_vec), VEC128_TYPE));
  return 0;
}

int InstrEmit_mtvscr(PPCHIRBuilder& f, const InstrData& i) {
  // is this the right format?
  // todo: what mtvscr does with the unused bits is implementation defined,
  // figure out what it does

  Value* v = f.LoadVR(i.VX128_1.RB);

  Value* has_njm_value = f.Extract(v, (uint8_t)3, INT32_TYPE);

  f.SetNJM(f.IsTrue(f.And(has_njm_value, f.LoadConstantInt32(65536))));

  f.StoreContext(offsetof(PPCContext, vscr_vec), v);
  return 0;
}

int InstrEmit_vaddcuw(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vaddfp_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD) <- (VA) + (VB) (4 x fp)
  Value* v = f.VectorAdd(f.LoadVR(va), f.LoadVR(vb), FLOAT32_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vaddfp(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vaddfp_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vaddfp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vaddfp_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vaddsbs(PPCHIRBuilder& f, const InstrData& i) {
  Value* v = f.VectorAdd(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE,
                         ARITHMETIC_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vaddshs(PPCHIRBuilder& f, const InstrData& i) {
  Value* v = f.VectorAdd(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE,
                         ARITHMETIC_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vaddsws(PPCHIRBuilder& f, const InstrData& i) {
  Value* v = f.VectorAdd(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT32_TYPE,
                         ARITHMETIC_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vaddubm(PPCHIRBuilder& f, const InstrData& i) {
  Value* v = f.VectorAdd(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE,
                         ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vaddubs(PPCHIRBuilder& f, const InstrData& i) {
  Value* v = f.VectorAdd(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE,
                         ARITHMETIC_UNSIGNED | ARITHMETIC_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vadduhm(PPCHIRBuilder& f, const InstrData& i) {
  Value* v = f.VectorAdd(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE,
                         ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vadduhs(PPCHIRBuilder& f, const InstrData& i) {
  Value* v = f.VectorAdd(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE,
                         ARITHMETIC_UNSIGNED | ARITHMETIC_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vadduwm(PPCHIRBuilder& f, const InstrData& i) {
  Value* v = f.VectorAdd(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT32_TYPE,
                         ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vadduws(PPCHIRBuilder& f, const InstrData& i) {
  Value* v = f.VectorAdd(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT32_TYPE,
                         ARITHMETIC_UNSIGNED | ARITHMETIC_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vand_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // VD <- (VA) & (VB)
  Value* v = f.And(f.LoadVR(va), f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vand(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vand_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vand128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vand_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vandc_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // VD <- (VA) & ¬(VB)
  Value* v = f.AndNot(f.LoadVR(va), f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vandc(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vandc_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vandc128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vandc_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vavgsb(PPCHIRBuilder& f, const InstrData& i) {
  Value* v =
      f.VectorAverage(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE, 0);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vavgsh(PPCHIRBuilder& f, const InstrData& i) {
  Value* v =
      f.VectorAverage(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE, 0);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vavgsw(PPCHIRBuilder& f, const InstrData& i) {
  // do i = 0 to 127 by 32
  //   aop = EXTS((VRA)i:i + 31)
  //   bop = EXTS((VRB)i:i + 31)
  //   VRTi:i + 31 = Chop((aop + int bop + int 1) >> 1, 32)
  Value* v =
      f.VectorAverage(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT32_TYPE, 0);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vavgub(PPCHIRBuilder& f, const InstrData& i) {
  Value* v = f.VectorAverage(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE,
                             ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vavguh(PPCHIRBuilder& f, const InstrData& i) {
  Value* v = f.VectorAverage(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE,
                             ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vavguw(PPCHIRBuilder& f, const InstrData& i) {
  Value* v = f.VectorAverage(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT32_TYPE,
                             ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vcfsx_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb,
                     uint32_t uimm) {
  // (VD) <- float(VB as signed) / 2^uimm
  Value* v = f.VectorConvertI2F(f.LoadVR(vb));
  if (uimm) {
    float fuimm = std::ldexp(1.0f, -int(uimm));
    v = f.Mul(v, f.Splat(f.LoadConstantFloat32(fuimm), VEC128_TYPE));
  }
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vcfsx(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcfsx_(f, i.VX.VD, i.VX.VB, i.VX.VA);
}
int InstrEmit_vcsxwfp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcfsx_(f, VX128_3_VD128, VX128_3_VB128, VX128_3_IMM);
}

int InstrEmit_vcfux_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb,
                     uint32_t uimm) {
  // (VD) <- float(VB as unsigned) / 2^uimm
  Value* v = f.VectorConvertI2F(f.LoadVR(vb), ARITHMETIC_UNSIGNED);
  if (uimm) {
    float fuimm = std::ldexp(1.0f, -int(uimm));
    v = f.Mul(v, f.Splat(f.LoadConstantFloat32(fuimm), VEC128_TYPE));
  }
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vcfux(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcfux_(f, i.VX.VD, i.VX.VB, i.VX.VA);
}
int InstrEmit_vcuxwfp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcfux_(f, VX128_3_VD128, VX128_3_VB128, VX128_3_IMM);
}

int InstrEmit_vctsxs_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb,
                      uint32_t uimm) {
  // (VD) <- int_sat(VB as signed * 2^uimm)
  float fuimm = static_cast<float>(std::exp2(uimm));
  Value* v =
      f.Mul(f.LoadVR(vb), f.Splat(f.LoadConstantFloat32(fuimm), VEC128_TYPE));
  v = f.VectorConvertF2I(v);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vctsxs(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vctsxs_(f, i.VX.VD, i.VX.VB, i.VX.VA);
}
int InstrEmit_vcfpsxws128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vctsxs_(f, VX128_3_VD128, VX128_3_VB128, VX128_3_IMM);
}

int InstrEmit_vctuxs_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb,
                      uint32_t uimm) {
  // (VD) <- int_sat(VB as unsigned * 2^uimm)
  float fuimm = static_cast<float>(std::exp2(uimm));
  Value* v =
      f.Mul(f.LoadVR(vb), f.Splat(f.LoadConstantFloat32(fuimm), VEC128_TYPE));
  v = f.VectorConvertF2I(v, ARITHMETIC_UNSIGNED);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vctuxs(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vctuxs_(f, i.VX.VD, i.VX.VB, i.VX.VA);
}
int InstrEmit_vcfpuxws128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vctuxs_(f, VX128_3_VD128, VX128_3_VB128, VX128_3_IMM);
}

int InstrEmit_vcmpbfp_(PPCHIRBuilder& f, const InstrData& i, uint32_t vd,
                       uint32_t va, uint32_t vb, uint32_t rc) {
  // if vA or vB are NaN, the 2 high-order bits are set (0xC0000000)
  Value* va_value = f.LoadVR(va);
  Value* vb_value = f.LoadVR(vb);
  Value* gt = f.VectorCompareSGT(va_value, vb_value, FLOAT32_TYPE);
  Value* lt =
      f.Not(f.VectorCompareSGE(va_value, f.Neg(vb_value), FLOAT32_TYPE));
  Value* v =
      f.Or(f.And(gt, f.LoadConstantVec128(vec128i(0x80000000, 0x80000000,
                                                  0x80000000, 0x80000000))),
           f.And(lt, f.LoadConstantVec128(vec128i(0x40000000, 0x40000000,
                                                  0x40000000, 0x40000000))));
  f.StoreVR(vd, v);
  if (rc) {
    // CR0:4 = 0; CR0:5 = VT == 0; CR0:6 = CR0:7 = 0;
    // If all of the elements are within bounds, CR6[2] is set
    // FIXME: Does not affect CR6[0], but the following function does.
    f.UpdateCR6(f.Or(gt, lt));
  }
  return 0;
}
int InstrEmit_vcmpbfp(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpbfp_(f, i, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
int InstrEmit_vcmpbfp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpbfp_(f, i, VX128_R_VD128, VX128_R_VA128, VX128_R_VB128,
                            i.VX128_R.Rc);
}

enum vcmpxxfp_op {
  vcmpxxfp_eq,
  vcmpxxfp_gt,
  vcmpxxfp_ge,
};
int InstrEmit_vcmpxxfp_(PPCHIRBuilder& f, const InstrData& i, vcmpxxfp_op cmpop,
                        uint32_t vd, uint32_t va, uint32_t vb, uint32_t rc) {
  // (VD.xyzw) = (VA.xyzw) OP (VB.xyzw) ? 0xFFFFFFFF : 0x00000000
  // if (Rc) CR6 = all_equal | 0 | none_equal | 0
  // If an element in either VA or VB is NaN the result will be 0x00000000
  Value* v;
  switch (cmpop) {
    case vcmpxxfp_eq:
      v = f.VectorCompareEQ(f.LoadVR(va), f.LoadVR(vb), FLOAT32_TYPE);
      break;
    case vcmpxxfp_gt:
      v = f.VectorCompareSGT(f.LoadVR(va), f.LoadVR(vb), FLOAT32_TYPE);
      break;
    case vcmpxxfp_ge:
      v = f.VectorCompareSGE(f.LoadVR(va), f.LoadVR(vb), FLOAT32_TYPE);
      break;
    default:
      assert_unhandled_case(cmpop);
      return 1;
  }
  if (rc) {
    f.UpdateCR6(v);
  }
  f.StoreVR(vd, v);
  return 0;
}

int InstrEmit_vcmpeqfp(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxfp_(f, i, vcmpxxfp_eq, i.VXR.VD, i.VXR.VA, i.VXR.VB,
                             i.VXR.Rc);
}
int InstrEmit_vcmpeqfp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxfp_(f, i, vcmpxxfp_eq, VX128_R_VD128, VX128_R_VA128,
                             VX128_R_VB128, i.VX128_R.Rc);
}
int InstrEmit_vcmpgefp(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxfp_(f, i, vcmpxxfp_ge, i.VXR.VD, i.VXR.VA, i.VXR.VB,
                             i.VXR.Rc);
}
int InstrEmit_vcmpgefp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxfp_(f, i, vcmpxxfp_ge, VX128_R_VD128, VX128_R_VA128,
                             VX128_R_VB128, i.VX128_R.Rc);
}
int InstrEmit_vcmpgtfp(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxfp_(f, i, vcmpxxfp_gt, i.VXR.VD, i.VXR.VA, i.VXR.VB,
                             i.VXR.Rc);
}
int InstrEmit_vcmpgtfp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxfp_(f, i, vcmpxxfp_gt, VX128_R_VD128, VX128_R_VA128,
                             VX128_R_VB128, i.VX128_R.Rc);
}

enum vcmpxxi_op {
  vcmpxxi_eq,
  vcmpxxi_gt_signed,
  vcmpxxi_gt_unsigned,
};
int InstrEmit_vcmpxxi_(PPCHIRBuilder& f, const InstrData& i, vcmpxxi_op cmpop,
                       uint32_t width, uint32_t vd, uint32_t va, uint32_t vb,
                       uint32_t rc) {
  // (VD.xyzw) = (VA.xyzw) OP (VB.xyzw) ? 0xFFFFFFFF : 0x00000000
  // if (Rc) CR6 = all_equal | 0 | none_equal | 0
  // If an element in either VA or VB is NaN the result will be 0x00000000
  Value* v;
  switch (cmpop) {
    case vcmpxxi_eq:
      switch (width) {
        case 1:
          v = f.VectorCompareEQ(f.LoadVR(va), f.LoadVR(vb), INT8_TYPE);
          break;
        case 2:
          v = f.VectorCompareEQ(f.LoadVR(va), f.LoadVR(vb), INT16_TYPE);
          break;
        case 4:
          v = f.VectorCompareEQ(f.LoadVR(va), f.LoadVR(vb), INT32_TYPE);
          break;
        default:
          assert_unhandled_case(width);
          return 1;
      }
      break;
    case vcmpxxi_gt_signed:
      switch (width) {
        case 1:
          v = f.VectorCompareSGT(f.LoadVR(va), f.LoadVR(vb), INT8_TYPE);
          break;
        case 2:
          v = f.VectorCompareSGT(f.LoadVR(va), f.LoadVR(vb), INT16_TYPE);
          break;
        case 4:
          v = f.VectorCompareSGT(f.LoadVR(va), f.LoadVR(vb), INT32_TYPE);
          break;
        default:
          assert_unhandled_case(width);
          return 1;
      }
      break;
    case vcmpxxi_gt_unsigned:
      switch (width) {
        case 1:
          v = f.VectorCompareUGT(f.LoadVR(va), f.LoadVR(vb), INT8_TYPE);
          break;
        case 2:
          v = f.VectorCompareUGT(f.LoadVR(va), f.LoadVR(vb), INT16_TYPE);
          break;
        case 4:
          v = f.VectorCompareUGT(f.LoadVR(va), f.LoadVR(vb), INT32_TYPE);
          break;
        default:
          assert_unhandled_case(width);
          return 1;
      }
      break;
    default:
      assert_unhandled_case(cmpop);
      return 1;
  }
  if (rc) {
    f.UpdateCR6(v);
  }
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vcmpequb(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_eq, 1, i.VXR.VD, i.VXR.VA, i.VXR.VB,
                            i.VXR.Rc);
}
int InstrEmit_vcmpequh(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_eq, 2, i.VXR.VD, i.VXR.VA, i.VXR.VB,
                            i.VXR.Rc);
}
int InstrEmit_vcmpequw(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_eq, 4, i.VXR.VD, i.VXR.VA, i.VXR.VB,
                            i.VXR.Rc);
}
int InstrEmit_vcmpequw128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_eq, 4, VX128_R_VD128, VX128_R_VA128,
                            VX128_R_VB128, i.VX128_R.Rc);
}
int InstrEmit_vcmpgtsb(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_gt_signed, 1, i.VXR.VD, i.VXR.VA,
                            i.VXR.VB, i.VXR.Rc);
}
int InstrEmit_vcmpgtsh(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_gt_signed, 2, i.VXR.VD, i.VXR.VA,
                            i.VXR.VB, i.VXR.Rc);
}
int InstrEmit_vcmpgtsw(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_gt_signed, 4, i.VXR.VD, i.VXR.VA,
                            i.VXR.VB, i.VXR.Rc);
}
int InstrEmit_vcmpgtub(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_gt_unsigned, 1, i.VXR.VD, i.VXR.VA,
                            i.VXR.VB, i.VXR.Rc);
}
int InstrEmit_vcmpgtuh(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_gt_unsigned, 2, i.VXR.VD, i.VXR.VA,
                            i.VXR.VB, i.VXR.Rc);
}
int InstrEmit_vcmpgtuw(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_gt_unsigned, 4, i.VXR.VD, i.VXR.VA,
                            i.VXR.VB, i.VXR.Rc);
}

int InstrEmit_vexptefp_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb) {
  // (VD) <- pow2(VB)
  Value* v = f.Pow2(f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vexptefp(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vexptefp_(f, i.VX.VD, i.VX.VB);
}
int InstrEmit_vexptefp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vexptefp_(f, VX128_3_VD128, VX128_3_VB128);
}

int InstrEmit_vlogefp_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb) {
  // (VD) <- log2(VB)
  Value* v = f.Log2(f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vlogefp(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vlogefp_(f, i.VX.VD, i.VX.VB);
}
int InstrEmit_vlogefp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vlogefp_(f, VX128_3_VD128, VX128_3_VB128);
}

int InstrEmit_vmaddfp_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb,
                       uint32_t vc) {
  /*
      chrispy: testing on POWER8 revealed that altivec vmaddfp unconditionally
     flushes denormal inputs to 0, regardless of NJM setting
  */
  Value* a = f.VectorDenormFlush(f.LoadVR(va));
  Value* b = f.VectorDenormFlush(f.LoadVR(vb));
  Value* c = f.VectorDenormFlush(f.LoadVR(vc));
  // (VD) <- ((VA) * (VC)) + (VB)
  Value* v = f.MulAdd(a, c, b);
  // todo: do denormal results also unconditionally become 0?
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vmaddfp(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- ((VA) * (VC)) + (VB)
  return InstrEmit_vmaddfp_(f, i.VXA.VD, i.VXA.VA, i.VXA.VB, i.VXA.VC);
}
int InstrEmit_vmaddfp128(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- ((VA) * (VB)) + (VD)
  // NOTE: this resuses VD and swaps the arg order!
  return InstrEmit_vmaddfp_(f, VX128_VD128, VX128_VA128, VX128_VD128,
                            VX128_VB128);
}

int InstrEmit_vmaddcfp128(PPCHIRBuilder& f, const InstrData& i) {
  /*
    see vmaddfp about these denormflushes
  */
  Value* a = f.VectorDenormFlush(f.LoadVR(VX128_VA128));
  Value* b = f.VectorDenormFlush(f.LoadVR(VX128_VB128));
  Value* d = f.VectorDenormFlush(f.LoadVR(VX128_VD128));
  // (VD) <- ((VA) * (VD)) + (VB)
  Value* v = f.MulAdd(a, d, b);
  f.StoreVR(VX128_VD128, v);
  return 0;
}

int InstrEmit_vmaxfp_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD) <- max((VA), (VB))
  Value* v = f.Max(f.LoadVR(va), f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vmaxfp(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vmaxfp_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vmaxfp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vmaxfp_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vmaxsb(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- max((VA), (VB)) (signed int8)
  Value* v = f.VectorMax(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vmaxsh(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- max((VA), (VB)) (signed int16)
  Value* v = f.VectorMax(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vmaxsw(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- max((VA), (VB)) (signed int32)
  Value* v = f.VectorMax(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT32_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vmaxub(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- max((VA), (VB)) (unsigned int8)
  Value* v = f.VectorMax(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE,
                         ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vmaxuh(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- max((VA), (VB)) (unsigned int16)
  Value* v = f.VectorMax(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE,
                         ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vmaxuw(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- max((VA), (VB)) (unsigned int32)
  Value* v = f.VectorMax(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT32_TYPE,
                         ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vmhaddshs(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmhraddshs(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vminfp_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD) <- min((VA), (VB))
  Value* v = f.Min(f.LoadVR(va), f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vminfp(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vminfp_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vminfp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vminfp_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vminsb(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- min((VA), (VB)) (signed int8)
  Value* v = f.VectorMin(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vminsh(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- min((VA), (VB)) (signed int16)
  Value* v = f.VectorMin(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vminsw(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- min((VA), (VB)) (signed int32)
  Value* v = f.VectorMin(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT32_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vminub(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- min((VA), (VB)) (unsigned int8)
  Value* v = f.VectorMin(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE,
                         ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vminuh(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- min((VA), (VB)) (unsigned int16)
  Value* v = f.VectorMin(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE,
                         ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vminuw(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- min((VA), (VB)) (unsigned int32)
  Value* v = f.VectorMin(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT32_TYPE,
                         ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vmladduhm(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmrghb(PPCHIRBuilder& f, const InstrData& i) {
  // (VD.b[i]) = (VA.b[i])
  // (VD.b[i+1]) = (VB.b[i+1])
  // ...
  Value* v =
      f.Permute(f.LoadConstantVec128(vec128b(0, 16, 1, 17, 2, 18, 3, 19, 4, 20,
                                             5, 21, 6, 22, 7, 23)),
                f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vmrghh(PPCHIRBuilder& f, const InstrData& i) {
  // (VD.w[i]) = (VA.w[i])
  // (VD.w[i+1]) = (VB.w[i+1])
  // ...
  Value* v = f.Permute(f.LoadConstantVec128(vec128s(0, 8, 1, 9, 2, 10, 3, 11)),
                       f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vmrghw_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD.x) = (VA.x)
  // (VD.y) = (VB.x)
  // (VD.z) = (VA.y)
  // (VD.w) = (VB.y)
  Value* v =
      f.Permute(f.LoadConstantUint32(MakePermuteMask(0, 0, 1, 0, 0, 1, 1, 1)),
                f.LoadVR(va), f.LoadVR(vb), INT32_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vmrghw(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vmrghw_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vmrghw128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vmrghw_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vmrglb(PPCHIRBuilder& f, const InstrData& i) {
  // (VD.b[i]) = (VA.b[i])
  // (VD.b[i+1]) = (VB.b[i+1])
  // ...
  Value* v =
      f.Permute(f.LoadConstantVec128(vec128b(8, 24, 9, 25, 10, 26, 11, 27, 12,
                                             28, 13, 29, 14, 30, 15, 31)),
                f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vmrglh(PPCHIRBuilder& f, const InstrData& i) {
  // (VD.w[i]) = (VA.w[i])
  // (VD.w[i+1]) = (VB.w[i+1])
  // ...
  Value* v =
      f.Permute(f.LoadConstantVec128(vec128s(4, 12, 5, 13, 6, 14, 7, 15)),
                f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vmrglw_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD.x) = (VA.z)
  // (VD.y) = (VB.z)
  // (VD.z) = (VA.w)
  // (VD.w) = (VB.w)
  Value* v =
      f.Permute(f.LoadConstantUint32(MakePermuteMask(0, 2, 1, 2, 0, 3, 1, 3)),
                f.LoadVR(va), f.LoadVR(vb), INT32_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vmrglw(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vmrglw_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vmrglw128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vmrglw_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vmsummbm(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmsumshm(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmsumshs(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmsumubm(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmsumuhm(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmsumuhs(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmsum3fp128(PPCHIRBuilder& f, const InstrData& i) {
  // Dot product XYZ.
  // (VD.xyzw) = (VA.x * VB.x) + (VA.y * VB.y) + (VA.z * VB.z)
  Value* v = f.DotProduct3(f.LoadVR(VX128_VA128), f.LoadVR(VX128_VB128));
  // chrispy: denormal outputs for Dot product are unconditionally made 0
  v = f.VectorDenormFlush(v);
  f.StoreVR(VX128_VD128, v);
  return 0;
}

int InstrEmit_vmsum4fp128(PPCHIRBuilder& f, const InstrData& i) {
  // Dot product XYZW.
  // (VD.xyzw) = (VA.x * VB.x) + (VA.y * VB.y) + (VA.z * VB.z) + (VA.w * VB.w)
  Value* v = f.DotProduct4(f.LoadVR(VX128_VA128), f.LoadVR(VX128_VB128));
  v = f.VectorDenormFlush(v);
  f.StoreVR(VX128_VD128, v);
  return 0;
}

int InstrEmit_vmulesb(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmulesh(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmuleub(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmuleuh(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmulosb(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmulosh(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmuloub(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmulouh(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmulfp128(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- (VA) * (VB) (4 x fp)
  Value* v = f.Mul(f.LoadVR(VX128_VA128), f.LoadVR(VX128_VB128));
  f.StoreVR(VX128_VD128, v);
  return 0;
}

int InstrEmit_vnmsubfp_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb,
                        uint32_t vc) {
  // (VD) <- -(((VA) * (VC)) - (VB))
  // NOTE: only one rounding should take place, but that's hard...
  // This really needs VFNMSUB132PS/VFNMSUB213PS/VFNMSUB231PS but that's AVX.
  // NOTE2: we could make vnmsub a new opcode, and then do it in double
  // precision, rounding after the neg

  /*
  chrispy: this is untested, but i believe this has the same DAZ behavior for
  inputs as vmadd
  */

  Value* a = f.VectorDenormFlush(f.LoadVR(va));
  Value* b = f.VectorDenormFlush(f.LoadVR(vb));
  Value* c = f.VectorDenormFlush(f.LoadVR(vc));

  Value* v = f.NegatedMulSub(a, c, b);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vnmsubfp(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vnmsubfp_(f, i.VXA.VD, i.VXA.VA, i.VXA.VB, i.VXA.VC);
}
int InstrEmit_vnmsubfp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vnmsubfp_(f, VX128_VD128, VX128_VA128, VX128_VD128,
                             VX128_VB128);
}

int InstrEmit_vnor_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // VD <- ¬((VA) | (VB))
  Value* v = f.Not(f.Or(f.LoadVR(va), f.LoadVR(vb)));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vnor(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vnor_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vnor128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vnor_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vor_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // VD <- (VA) | (VB)
  if (va == vb) {
    // Copy VA==VB into VD.
    f.StoreVR(vd, f.LoadVR(va));
  } else {
    Value* v = f.Or(f.LoadVR(va), f.LoadVR(vb));
    f.StoreVR(vd, v);
  }
  return 0;
}
int InstrEmit_vor(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vor_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vor128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vor_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vperm_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb,
                     uint32_t vc) {
  Value* v = f.Permute(f.LoadVR(vc), f.LoadVR(va), f.LoadVR(vb), INT8_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vperm(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vperm_(f, i.VXA.VD, i.VXA.VA, i.VXA.VB, i.VXA.VC);
}
int InstrEmit_vperm128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vperm_(f, VX128_2_VD128, VX128_2_VA128, VX128_2_VB128,
                          VX128_2_VC);
}

int InstrEmit_vpermwi128(PPCHIRBuilder& f, const InstrData& i) {
  // (VD.x) = (VB.uimm[6-7])
  // (VD.y) = (VB.uimm[4-5])
  // (VD.z) = (VB.uimm[2-3])
  // (VD.w) = (VB.uimm[0-1])
  const uint32_t vd = i.VX128_P.VD128l | (i.VX128_P.VD128h << 5);
  const uint32_t vb = i.VX128_P.VB128l | (i.VX128_P.VB128h << 5);
  uint32_t uimm = i.VX128_P.PERMl | (i.VX128_P.PERMh << 5);
  uint32_t mask = MakeSwizzleMask(uimm >> 6, uimm >> 4, uimm >> 2, uimm >> 0);
  Value* v = f.Swizzle(f.LoadVR(vb), INT32_TYPE, mask);
  f.StoreVR(vd, v);
  return 0;
}

int InstrEmit_vrefp_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb) {
  // (VD) <- 1/(VB)
  Value* v = f.Recip(f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vrefp(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vrefp_(f, i.VX.VD, i.VX.VB);
}
int InstrEmit_vrefp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vrefp_(f, VX128_3_VD128, VX128_3_VB128);
}

int InstrEmit_vrfim_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb) {
  // (VD) <- RndToFPInt32Floor(VB)
  Value* v = f.Round(f.LoadVR(vb), ROUND_TO_MINUS_INFINITY);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vrfim(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vrfim_(f, i.VX.VD, i.VX.VB);
}
int InstrEmit_vrfim128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vrfim_(f, VX128_3_VD128, VX128_3_VB128);
}

int InstrEmit_vrfin_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb) {
  // (VD) <- RoundToNearest(VB)
  Value* v = f.Round(f.LoadVR(vb), ROUND_TO_NEAREST);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vrfin(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vrfin_(f, i.VX.VD, i.VX.VB);
}
int InstrEmit_vrfin128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vrfin_(f, VX128_3_VD128, VX128_3_VB128);
}

int InstrEmit_vrfip_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb) {
  // (VD) <- RndToFPInt32Ceil(VB)
  Value* v = f.Round(f.LoadVR(vb), ROUND_TO_POSITIVE_INFINITY);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vrfip(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vrfip_(f, i.VX.VD, i.VX.VB);
}
int InstrEmit_vrfip128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vrfip_(f, VX128_3_VD128, VX128_3_VB128);
}

int InstrEmit_vrfiz_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb) {
  // (VD) <- RndToFPInt32Trunc(VB)
  Value* v = f.Round(f.LoadVR(vb), ROUND_TO_ZERO);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vrfiz(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vrfiz_(f, i.VX.VD, i.VX.VB);
}
int InstrEmit_vrfiz128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vrfiz_(f, VX128_3_VD128, VX128_3_VB128);
}

int InstrEmit_vrlb(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- ROTL((VA), (VB)&0x3)
  Value* v =
      f.VectorRotateLeft(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vrlh(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- ROTL((VA), (VB)&0xF)
  Value* v =
      f.VectorRotateLeft(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vrlw_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD) <- ROTL((VA), (VB)&0x1F)
  Value* v = f.VectorRotateLeft(f.LoadVR(va), f.LoadVR(vb), INT32_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vrlw(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vrlw_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vrlw128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vrlw_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vrlimi128(PPCHIRBuilder& f, const InstrData& i) {
  const uint32_t vd = i.VX128_4.VD128l | (i.VX128_4.VD128h << 5);
  const uint32_t vb = i.VX128_4.VB128l | (i.VX128_4.VB128h << 5);
  uint32_t blend_mask_src = i.VX128_4.IMM;
  uint32_t blend_mask = 0;
  blend_mask |= (((blend_mask_src >> 3) & 0x1) ? 0 : 4) << 0;
  blend_mask |= (((blend_mask_src >> 2) & 0x1) ? 1 : 5) << 8;
  blend_mask |= (((blend_mask_src >> 1) & 0x1) ? 2 : 6) << 16;
  blend_mask |= (((blend_mask_src >> 0) & 0x1) ? 3 : 7) << 24;
  uint32_t rotate = i.VX128_4.z;
  // This is just a fancy permute.
  // X Y Z W, rotated left by 2 = Z W X Y
  // Then mask select the results into the dest.
  // Sometimes rotation is zero, so fast path.
  Value* v;
  if (rotate) {
    // TODO(benvanik): constants need conversion.
    uint32_t swizzle_mask;
    switch (rotate) {
      case 1:
        // X Y Z W -> Y Z W X
        swizzle_mask = SWIZZLE_XYZW_TO_YZWX;
        break;
      case 2:
        // X Y Z W -> Z W X Y
        swizzle_mask = SWIZZLE_XYZW_TO_ZWXY;
        break;
      case 3:
        // X Y Z W -> W X Y Z
        swizzle_mask = SWIZZLE_XYZW_TO_WXYZ;
        break;
      default:
        XEINSTRNOTIMPLEMENTED();
        return 1;
    }
    v = f.Swizzle(f.LoadVR(vb), FLOAT32_TYPE, swizzle_mask);
  } else {
    v = f.LoadVR(vb);
  }
  if (blend_mask != kIdentityPermuteMask) {
    v = f.Permute(f.LoadConstantUint32(blend_mask), v, f.LoadVR(vd),
                  INT32_TYPE);
  }
  f.StoreVR(vd, v);
  return 0;
}

int InstrEmit_vrsqrtefp_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb) {
  // (VD) <- 1 / sqrt(VB)
  // There are a lot of rules in the Altivec_PEM docs for handlings that
  // result in nan/infinity/etc. They are ignored here. I hope games would
  // never rely on them.
  Value* v = f.RSqrt(f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vrsqrtefp(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vrsqrtefp_(f, i.VX.VD, i.VX.VB);
}
int InstrEmit_vrsqrtefp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vrsqrtefp_(f, VX128_3_VD128, VX128_3_VB128);
}

int InstrEmit_vsel_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb,
                    uint32_t vc) {
  // For each bit:
  // VRTi <- ((VRC)i=0) ? (VRA)i : (VRB)i
  Value* v = f.Select(f.LoadVR(vc), f.LoadVR(va), f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vsel(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vsel_(f, i.VXA.VD, i.VXA.VA, i.VXA.VB, i.VXA.VC);
}
int InstrEmit_vsel128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vsel_(f, VX128_VD128, VX128_VA128, VX128_VB128, VX128_VD128);
}
// chrispy: this is test code for checking whether a game takes advantage of the
// VSR/VSL undocumented/undefined variable shift behavior
static void AssertShiftElementsOk(PPCHIRBuilder& f, Value* v) {
#if 0
  Value* splatted = f.Splat(f.Extract(v, (uint8_t)0, INT8_TYPE), VEC128_TYPE);

  Value* checkequal = f.Xor(splatted, v);
  f.DebugBreakTrue(f.IsTrue(checkequal));
#endif
}
int InstrEmit_vsl(PPCHIRBuilder& f, const InstrData& i) {
  Value* va = f.LoadVR(i.VX.VA);
  Value* vb = f.LoadVR(i.VX.VB);

  AssertShiftElementsOk(f, vb);
  Value* v =
      f.Shl(va, f.And(f.Extract(vb, 15, INT8_TYPE), f.LoadConstantInt8(0b111)));
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vslb(PPCHIRBuilder& f, const InstrData& i) {
  Value* v = f.VectorShl(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vslh(PPCHIRBuilder& f, const InstrData& i) {
  Value* v = f.VectorShl(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vslw_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // VA = |xxxxx|yyyyy|zzzzz|wwwww|
  // VB = |...sh|...sh|...sh|...sh|
  // VD = |x<<sh|y<<sh|z<<sh|w<<sh|
  Value* v = f.VectorShl(f.LoadVR(va), f.LoadVR(vb), INT32_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vslw(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vslw_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vslw128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vslw_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

static const vec128_t __vsldoi_table[16] = {
    vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
    vec128b(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16),
    vec128b(2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17),
    vec128b(3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18),
    vec128b(4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19),
    vec128b(5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20),
    vec128b(6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
    vec128b(7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
    vec128b(8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23),
    vec128b(9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
    vec128b(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25),
    vec128b(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26),
    vec128b(12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
    vec128b(13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28),
    vec128b(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29),
    vec128b(15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30),
};
int InstrEmit_vsldoi_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb,
                      uint32_t sh) {
  // (VD) <- ((VA) || (VB)) << (SH << 3)
  if (!sh) {
    f.StoreVR(vd, f.LoadVR(va));
    return 0;
  } else if (sh == 16) {
    f.StoreVR(vd, f.LoadVR(vb));
    return 0;
  }
  // TODO(benvanik): optimize for the rotation case:
  // vsldoi128 vr63,vr63,vr63,4
  // (ABCD ABCD) << 4b = (BCDA)
  // (VA << SH) OR (VB >> (16 - SH))
  Value* control = f.LoadConstantVec128(__vsldoi_table[sh]);
  Value* v = f.Permute(control, f.LoadVR(va), f.LoadVR(vb), INT8_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vsldoi(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vsldoi_(f, i.VXA.VD, i.VXA.VA, i.VXA.VB, i.VXA.VC & 0xF);
}
int InstrEmit_vsldoi128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vsldoi_(f, VX128_5_VD128, VX128_5_VA128, VX128_5_VB128,
                           VX128_5_SH);
}

int InstrEmit_vslo_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD) <- (VA) << (VB.b[F] & 0x78) (by octet)
  // TODO(benvanik): flag for shift-by-octet as optimization.
  Value* sh = f.Shr(
      f.And(f.Extract(f.LoadVR(vb), 15, INT8_TYPE), f.LoadConstantInt8(0x78)),
      3);
  Value* v = f.Permute(f.LoadVectorShl(sh), f.LoadVR(va), f.LoadZeroVec128(),
                       INT8_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vslo(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vslo_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vslo128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vslo_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vspltb(PPCHIRBuilder& f, const InstrData& i) {
  // b <- UIMM*8
  // do i = 0 to 127 by 8
  //  (VD)[i:i+7] <- (VB)[b:b+7]
  Value* b = f.Extract(f.LoadVR(i.VX.VB), i.VX.VA & 0xF, INT8_TYPE);
  Value* v = f.Splat(b, VEC128_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsplth(PPCHIRBuilder& f, const InstrData& i) {
  // (VD.xyzw) <- (VB.uimm)
  Value* h = f.Extract(f.LoadVR(i.VX.VB), i.VX.VA & 0x7, INT16_TYPE);
  Value* v = f.Splat(h, VEC128_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vspltw_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb,
                      uint32_t uimm) {
  // (VD.xyzw) <- (VB.uimm)
  Value* w = f.Extract(f.LoadVR(vb), uimm & 0x3, INT32_TYPE);
  Value* v = f.Splat(w, VEC128_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vspltw(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vspltw_(f, i.VX.VD, i.VX.VB, i.VX.VA);
}
int InstrEmit_vspltw128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vspltw_(f, VX128_3_VD128, VX128_3_VB128, VX128_3_IMM);
}

int InstrEmit_vspltisb(PPCHIRBuilder& f, const InstrData& i) {
  // (VD.xyzw) <- sign_extend(uimm)
  Value* v;
  if (i.VX.VA) {
    // Sign extend from 5bits -> 8 and load.
    int8_t simm = (i.VX.VA & 0x10) ? (i.VX.VA | 0xF0) : i.VX.VA;
    v = f.Splat(f.LoadConstantInt8(simm), VEC128_TYPE);
  } else {
    // Zero out the register.
    v = f.LoadZeroVec128();
  }
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vspltish(PPCHIRBuilder& f, const InstrData& i) {
  // (VD.xyzw) <- sign_extend(uimm)
  Value* v;
  if (i.VX.VA) {
    // Sign extend from 5bits -> 16 and load.
    int16_t simm = (i.VX.VA & 0x10) ? (i.VX.VA | 0xFFF0) : i.VX.VA;
    v = f.Splat(f.LoadConstantInt16(simm), VEC128_TYPE);
  } else {
    // Zero out the register.
    v = f.LoadZeroVec128();
  }
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vspltisw_(PPCHIRBuilder& f, uint32_t vd, uint32_t uimm) {
  // (VD.xyzw) <- sign_extend(uimm)
  Value* v;
  if (uimm) {
    // Sign extend from 5bits -> 32 and load.
    int32_t simm = (uimm & 0x10) ? (uimm | 0xFFFFFFF0) : uimm;
    v = f.Splat(f.LoadConstantInt32(simm), VEC128_TYPE);
  } else {
    // Zero out the register.
    v = f.LoadZeroVec128();
  }
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vspltisw(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vspltisw_(f, i.VX.VD, i.VX.VA);
}
int InstrEmit_vspltisw128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vspltisw_(f, VX128_3_VD128, VX128_3_IMM);
}

int InstrEmit_vsr(PPCHIRBuilder& f, const InstrData& i) {
  Value* va = f.LoadVR(i.VX.VA);
  Value* vb = f.LoadVR(i.VX.VB);

  AssertShiftElementsOk(f, vb);

  Value* v =
      f.Shr(va, f.And(f.Extract(vb, 15, INT8_TYPE), f.LoadConstantInt8(0b111)));
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsrab(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- (VA) >>a (VB) by bytes
  Value* v = f.VectorSha(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsrah(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- (VA) >>a (VB) by halfwords
  Value* v = f.VectorSha(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsraw_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD) <- (VA) >>a (VB) by words
  Value* v = f.VectorSha(f.LoadVR(va), f.LoadVR(vb), INT32_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vsraw(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vsraw_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vsraw128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vsraw_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vsrb(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- (VA) >> (VB) by bytes
  Value* v = f.VectorShr(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsrh(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- (VA) >> (VB) by halfwords
  Value* v = f.VectorShr(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsro_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD) <- (VA) >> (VB.b[F] & 0x78) (by octet)
  // TODO(benvanik): flag for shift-by-octet as optimization.
  Value* sh = f.Shr(
      f.And(f.Extract(f.LoadVR(vb), 15, INT8_TYPE), f.LoadConstantInt8(0x78)),
      3);
  Value* v = f.Permute(f.LoadVectorShr(sh), f.LoadZeroVec128(), f.LoadVR(va),
                       INT8_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vsro(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vsro_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vsro128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vsro_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vsrw_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD) <- (VA) >> (VB) by words
  Value* v = f.VectorShr(f.LoadVR(va), f.LoadVR(vb), INT32_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vsrw(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vsrw_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vsrw128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vsrw_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vsubcuw(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vsubfp_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD) <- (VA) - (VB) (4 x fp)
  Value* v = f.VectorSub(f.LoadVR(va), f.LoadVR(vb), FLOAT32_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vsubfp(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vsubfp_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vsubfp128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vsubfp_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vsubsbs(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- clamp(EXTS(VA) + ¬EXTS(VB) + 1, -128, 127)
  Value* v = f.VectorSub(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE,
                         ARITHMETIC_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsubshs(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- clamp(EXTS(VA) + ¬EXTS(VB) + 1, -2^15, 2^15-1)
  Value* v = f.VectorSub(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE,
                         ARITHMETIC_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsubsws(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- clamp(EXTS(VA) + ¬EXTS(VB) + 1, -2^31, 2^31-1)
  Value* v = f.VectorSub(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT32_TYPE,
                         ARITHMETIC_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsububm(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- (EXTZ(VA) + ¬EXTZ(VB) + 1) % 256
  Value* v = f.VectorSub(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE,
                         ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsubuhm(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- (EXTZ(VA) + ¬EXTZ(VB) + 1) % 2^16
  Value* v = f.VectorSub(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE,
                         ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsubuwm(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- (EXTZ(VA) + ¬EXTZ(VB) + 1) % 2^32
  Value* v = f.VectorSub(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT32_TYPE,
                         ARITHMETIC_UNSIGNED);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsububs(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- clamp(EXTZ(VA) + ¬EXTZ(VB) + 1, 0, 256)
  Value* v = f.VectorSub(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE,
                         ARITHMETIC_SATURATE | ARITHMETIC_UNSIGNED);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsubuhs(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- clamp(EXTZ(VA) + ¬EXTZ(VB) + 1, 0, 2^16-1)
  Value* v = f.VectorSub(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE,
                         ARITHMETIC_SATURATE | ARITHMETIC_UNSIGNED);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsubuws(PPCHIRBuilder& f, const InstrData& i) {
  // (VD) <- clamp(EXTZ(VA) + ¬EXTZ(VB) + 1, 0, 2^32-1)
  Value* v = f.VectorSub(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT32_TYPE,
                         ARITHMETIC_SATURATE | ARITHMETIC_UNSIGNED);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vsumsws(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vsum2sws(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vsum4sbs(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vsum4shs(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vsum4ubs(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

static Value* vkpkx_in_low(PPCHIRBuilder& f, Value* input) {
  // truncate from argb8888 to 1 bit alpha, 5 bit red, 5 bit green, 5 bit blue
  auto ShrU32Vec = [&f](Value* input, unsigned shift) {
    return f.VectorShr(input, f.LoadConstantVec128(vec128i(shift)), INT32_TYPE);
  };
  auto AndU32Vec = [&f](Value* input, unsigned msk) {
    return f.And(input, f.LoadConstantVec128(vec128i(msk)));
  };
  auto tmp1 = AndU32Vec(ShrU32Vec(input, 9), 0xFC00);
  auto tmp2 = AndU32Vec(ShrU32Vec(input, 6), 0x3E0);
  auto tmp3 = AndU32Vec(ShrU32Vec(input, 3), 0x1F);
  return f.Or(tmp3, f.Or(tmp1, tmp2));
}

int InstrEmit_vpkpx(PPCHIRBuilder& f, const InstrData& i) {
  // I compared the results of this against over a million randomly generated
  // sets of inputs and all compared equal

  Value* src1 = f.LoadVR(i.VX.VA);

  Value* src2 = f.LoadVR(i.VX.VB);

  Value* pck1 = vkpkx_in_low(f, src1);
  Value* pck2 = vkpkx_in_low(f, src2);

  Value* result = f.Pack(
      pck1, pck2,
      PACK_TYPE_16_IN_32 | PACK_TYPE_IN_UNSIGNED | PACK_TYPE_OUT_UNSIGNED);

  f.StoreVR(i.VX.VD, result);

  return 0;
}

int InstrEmit_vpkshss_(PPCHIRBuilder& f, uint32_t vd, uint32_t va,
                       uint32_t vb) {
  // Vector Pack Signed Halfword Signed Saturate
  // Convert VA and VB from signed words to signed saturated bytes then
  // concat:
  // for each i in VA + VB:
  //   i = int8_t(Clamp(EXTS(int16_t(t)), -128, 127))
  // dest = VA | VB (lower 8bit values)
  Value* v = f.Pack(f.LoadVR(va), f.LoadVR(vb),
                    PACK_TYPE_8_IN_16 | PACK_TYPE_IN_SIGNED |
                        PACK_TYPE_OUT_SIGNED | PACK_TYPE_OUT_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vpkshss(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkshss_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vpkshss128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkshss_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vpkswss_(PPCHIRBuilder& f, uint32_t vd, uint32_t va,
                       uint32_t vb) {
  // Vector Pack Signed Word Signed Saturate
  // Convert VA and VB from signed int words to signed saturated shorts then
  // concat:
  // for each i in VA + VB:
  //   i = int16_t(Clamp(EXTS(int32_t(t)), -2^15, 2^15-1))
  // dest = VA | VB (lower 16bit values)
  Value* v = f.Pack(f.LoadVR(va), f.LoadVR(vb),
                    PACK_TYPE_16_IN_32 | PACK_TYPE_IN_SIGNED |
                        PACK_TYPE_OUT_SIGNED | PACK_TYPE_OUT_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vpkswss(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkswss_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vpkswss128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkswss_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vpkswus_(PPCHIRBuilder& f, uint32_t vd, uint32_t va,
                       uint32_t vb) {
  // Vector Pack Signed Word Unsigned Saturate
  // Convert VA and VB from signed int words to unsigned saturated shorts then
  // concat:
  // for each i in VA + VB:
  //   i = uint16_t(Clamp(EXTS(int32_t(t)), 0, 2^16-1))
  // dest = VA | VB (lower 16bit values)
  Value* v = f.Pack(f.LoadVR(va), f.LoadVR(vb),
                    PACK_TYPE_16_IN_32 | PACK_TYPE_IN_SIGNED |
                        PACK_TYPE_OUT_UNSIGNED | PACK_TYPE_OUT_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vpkswus(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkswus_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vpkswus128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkswus_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vpkuhum_(PPCHIRBuilder& f, uint32_t vd, uint32_t va,
                       uint32_t vb) {
  // Vector Pack Unsigned Halfword Unsigned Modulo
  // Convert VA and VB from unsigned shorts to unsigned bytes then concat:
  // for each i in VA + VB:
  //   i = uint8_t(uint16_t(i))
  // dest = VA | VB (lower 8bit values)
  Value* v = f.Pack(f.LoadVR(va), f.LoadVR(vb),
                    PACK_TYPE_8_IN_16 | PACK_TYPE_IN_UNSIGNED |
                        PACK_TYPE_OUT_UNSIGNED | PACK_TYPE_OUT_UNSATURATE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vpkuhum(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkuhum_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vpkuhum128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkuhum_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vpkuhus_(PPCHIRBuilder& f, uint32_t vd, uint32_t va,
                       uint32_t vb) {
  // Vector Pack Unsigned Halfword Unsigned Saturate
  // Convert VA and VB from unsigned shorts to unsigned saturated bytes then
  // concat:
  // for each i in VA + VB:
  //   i = uint8_t(Clamp(EXTZ(uint16_t(i)), 0, 255))
  // dest = VA | VB (lower 8bit values)
  Value* v = f.Pack(f.LoadVR(va), f.LoadVR(vb),
                    PACK_TYPE_8_IN_16 | PACK_TYPE_IN_UNSIGNED |
                        PACK_TYPE_OUT_UNSIGNED | PACK_TYPE_OUT_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vpkuhus(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkuhus_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vpkuhus128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkuhus_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vpkshus_(PPCHIRBuilder& f, uint32_t vd, uint32_t va,
                       uint32_t vb) {
  // Vector Pack Signed Halfword Unsigned Saturate
  // Convert VA and VB from signed shorts to unsigned saturated bytes then
  // concat:
  // for each i in VA + VB:
  //   i = uint8_t(Clamp(EXTS(int16_t(i)), 0, 255))
  // dest = VA | VB (lower 8bit values)
  Value* v = f.Pack(f.LoadVR(va), f.LoadVR(vb),
                    PACK_TYPE_8_IN_16 | PACK_TYPE_IN_SIGNED |
                        PACK_TYPE_OUT_UNSIGNED | PACK_TYPE_OUT_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vpkshus(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkshus_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vpkshus128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkshus_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vpkuwum_(PPCHIRBuilder& f, uint32_t vd, uint32_t va,
                       uint32_t vb) {
  // Vector Pack Unsigned Word Unsigned Modulo
  // Concat low shorts from VA + VB:
  // for each i in VA + VB:
  //   i = uint16_t(uint32_t(i))
  // dest = VA | VB (lower 16bit values)
  Value* v = f.Pack(f.LoadVR(va), f.LoadVR(vb),
                    PACK_TYPE_16_IN_32 | PACK_TYPE_IN_UNSIGNED |
                        PACK_TYPE_OUT_UNSIGNED | PACK_TYPE_OUT_UNSATURATE);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vpkuwum(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkuwum_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vpkuwum128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkuwum_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vpkuwus_(PPCHIRBuilder& f, uint32_t vd, uint32_t va,
                       uint32_t vb) {
  // Vector Pack Unsigned Word Unsigned Saturate
  // Convert VA and VB from unsigned int words to unsigned saturated shorts then
  // concat:
  // for each i in VA + VB:
  //   i = uint16_t(Clamp(EXTZ(uint32_t(t)), 0, 2^16-1))
  // dest = VA | VB (lower 16bit values)
  Value* v = f.Pack(f.LoadVR(va), f.LoadVR(vb),
                    PACK_TYPE_16_IN_32 | PACK_TYPE_IN_UNSIGNED |
                        PACK_TYPE_OUT_UNSIGNED | PACK_TYPE_OUT_SATURATE);
  f.StoreSAT(f.DidSaturate(v));
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vpkuwus(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkuwus_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vpkuwus128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vpkuwus_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vupkhpx(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vupklpx(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vupkhsh_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb) {
  // Vector Unpack High Signed Halfword
  // halfwords 0-3 expanded to words 0-3 and sign extended
  Value* v =
      f.Unpack(f.LoadVR(vb), PACK_TYPE_TO_HI | PACK_TYPE_16_IN_32 |
                                 PACK_TYPE_IN_SIGNED | PACK_TYPE_OUT_SIGNED);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vupkhsh(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vupkhsh_(f, i.VX.VD, i.VX.VB);
}
int InstrEmit_vupkhsh128(PPCHIRBuilder& f, const InstrData& i) {
  assert_zero(VX128_VA128);
  return InstrEmit_vupkhsh_(f, VX128_VD128, VX128_VB128);
}

int InstrEmit_vupklsh_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb) {
  // Vector Unpack Low Signed Halfword
  // halfwords 4-7 expanded to words 0-3 and sign extended
  Value* v =
      f.Unpack(f.LoadVR(vb), PACK_TYPE_TO_LO | PACK_TYPE_16_IN_32 |
                                 PACK_TYPE_IN_SIGNED | PACK_TYPE_OUT_SIGNED);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vupklsh(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vupklsh_(f, i.VX.VD, i.VX.VB);
}
int InstrEmit_vupklsh128(PPCHIRBuilder& f, const InstrData& i) {
  assert_zero(VX128_VA128);
  return InstrEmit_vupklsh_(f, VX128_VD128, VX128_VB128);
}

int InstrEmit_vupkhsb_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb) {
  // Vector Unpack High Signed Byte
  // bytes 0-7 expanded to halfwords 0-7 and sign extended
  Value* v =
      f.Unpack(f.LoadVR(vb), PACK_TYPE_TO_HI | PACK_TYPE_8_IN_16 |
                                 PACK_TYPE_IN_SIGNED | PACK_TYPE_OUT_SIGNED);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vupkhsb(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vupkhsb_(f, i.VX.VD, i.VX.VB);
}
int InstrEmit_vupkhsb128(PPCHIRBuilder& f, const InstrData& i) {
  uint32_t va = VX128_VA128;
  if (va == 0x60) {
    // Hrm, my instruction tables suck.
    return InstrEmit_vupkhsh_(f, VX128_VD128, VX128_VB128);
  }
  return InstrEmit_vupkhsb_(f, VX128_VD128, VX128_VB128);
}

int InstrEmit_vupklsb_(PPCHIRBuilder& f, uint32_t vd, uint32_t vb) {
  // Vector Unpack Low Signed Byte
  // bytes 8-15 expanded to halfwords 0-7 and sign extended
  Value* v =
      f.Unpack(f.LoadVR(vb), PACK_TYPE_TO_LO | PACK_TYPE_8_IN_16 |
                                 PACK_TYPE_IN_SIGNED | PACK_TYPE_OUT_SIGNED);
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vupklsb(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vupklsb_(f, i.VX.VD, i.VX.VB);
}
int InstrEmit_vupklsb128(PPCHIRBuilder& f, const InstrData& i) {
  uint32_t va = VX128_VA128;
  if (va == 0x60) {
    // Hrm, my instruction tables suck.
    return InstrEmit_vupklsh_(f, VX128_VD128, VX128_VB128);
  }
  return InstrEmit_vupklsb_(f, VX128_VD128, VX128_VB128);
}

int InstrEmit_vpkd3d128(PPCHIRBuilder& f, const InstrData& i) {
  const uint32_t vd = i.VX128_4.VD128l | (i.VX128_4.VD128h << 5);
  const uint32_t vb = i.VX128_4.VB128l | (i.VX128_4.VB128h << 5);
  uint32_t type = i.VX128_4.IMM >> 2;
  uint32_t pack = i.VX128_4.IMM & 0x3;
  uint32_t shift = i.VX128_4.z;
  Value* v = f.LoadVR(vb);
  switch (type) {
    case 0:  // VPACK_D3DCOLOR
      v = f.Pack(v, PACK_TYPE_D3DCOLOR);
      break;
    case 1:  // VPACK_NORMSHORT2
      v = f.Pack(v, PACK_TYPE_SHORT_2);
      break;
    case 2:  // VPACK_NORMPACKED32 2_10_10_10 w_z_y_x
      v = f.Pack(v, PACK_TYPE_UINT_2101010);
      break;
    case 3:  // VPACK_FLOAT16_2 DXGI_FORMAT_R16G16_FLOAT
      v = f.Pack(v, PACK_TYPE_FLOAT16_2);
      break;
    case 4:  // VPACK_NORMSHORT4
      v = f.Pack(v, PACK_TYPE_SHORT_4);
      break;
    case 5:  // VPACK_FLOAT16_4 DXGI_FORMAT_R16G16B16A16_FLOAT
      v = f.Pack(v, PACK_TYPE_FLOAT16_4);
      break;
    case 6:  // VPACK_NORMPACKED64 4_20_20_20 w_z_y_x
      // Used in 54540829 and other installments in the series, pretty rarely in
      // general.
      v = f.Pack(v, PACK_TYPE_ULONG_4202020);
      break;
    default:
      assert_unhandled_case(type);
      return 1;
  }
  // https://hlssmod.net/he_code/public/pixelwriter.h
  // control = prev:0123 | new:4567
  uint32_t control = kIdentityPermuteMask;  // original
  switch (pack) {
    case 1:  // VPACK_32
             // VPACK_32 & shift = 3 puts lower 32 bits in x (leftmost slot).
      switch (shift) {
        case 0:
          control = MakePermuteMask(0, 0, 0, 1, 0, 2, 1, 3);
          break;
        case 1:
          control = MakePermuteMask(0, 0, 0, 1, 1, 3, 0, 3);
          break;
        case 2:
          control = MakePermuteMask(0, 0, 1, 3, 0, 2, 0, 3);
          break;
        case 3:
          control = MakePermuteMask(1, 3, 0, 1, 0, 2, 0, 3);
          break;
        default:
          assert_unhandled_case(shift);
          return 1;
      }
      break;
    case 2:  // 64bit
      switch (shift) {
        case 0:
          control = MakePermuteMask(0, 0, 0, 1, 1, 2, 1, 3);
          break;
        case 1:
          control = MakePermuteMask(0, 0, 1, 2, 1, 3, 0, 3);
          break;
        case 2:
          control = MakePermuteMask(1, 2, 1, 3, 0, 2, 0, 3);
          break;
        case 3:
          control = MakePermuteMask(1, 3, 0, 1, 0, 2, 0, 3);
          break;
        default:
          assert_unhandled_case(shift);
          return 1;
      }
      break;
    case 3:  // 64bit
      switch (shift) {
        case 0:
          control = MakePermuteMask(0, 0, 0, 1, 1, 2, 1, 3);
          break;
        case 1:
          control = MakePermuteMask(0, 0, 1, 2, 1, 3, 0, 3);
          break;
        case 2:
          control = MakePermuteMask(1, 2, 1, 3, 0, 2, 0, 3);
          break;
        case 3:
          control = MakePermuteMask(0, 0, 0, 1, 0, 2, 1, 2);
          break;
        default:
          assert_unhandled_case(shift);
          return 1;
      }
      break;
    default:
      assert_unhandled_case(pack);
      return 1;
  }
  v = f.Permute(f.LoadConstantUint32(control), f.LoadVR(vd), v, INT32_TYPE);
  f.StoreVR(vd, v);
  return 0;
}

int InstrEmit_vupkd3d128(PPCHIRBuilder& f, const InstrData& i) {
  // Can't find many docs on this. Best reference is
  // https://code.google.com/archive/p/worldcraft/source/default/source?page=4
  // (qylib/math/xmmatrix.inl)
  // which shows how it's used in some cases. Since it's all intrinsics,
  // finding it in code is pretty easy.
  const uint32_t vd = i.VX128_3.VD128l | (i.VX128_3.VD128h << 5);
  const uint32_t vb = i.VX128_3.VB128l | (i.VX128_3.VB128h << 5);
  const uint32_t type = i.VX128_3.IMM >> 2;
  Value* v = f.LoadVR(vb);
  switch (type) {
    case 0:  // VPACK_D3DCOLOR
      v = f.Unpack(v, PACK_TYPE_D3DCOLOR);
      break;
    case 1:  // VPACK_NORMSHORT2
      v = f.Unpack(v, PACK_TYPE_SHORT_2);
      break;
    case 2:  // VPACK_NORMPACKED32 2_10_10_10 w_z_y_x
      v = f.Unpack(v, PACK_TYPE_UINT_2101010);
      break;
    case 3:  // VPACK_FLOAT16_2 DXGI_FORMAT_R16G16_FLOAT
      v = f.Unpack(v, PACK_TYPE_FLOAT16_2);
      break;
    case 4:  // VPACK_NORMSHORT4
      v = f.Unpack(v, PACK_TYPE_SHORT_4);
      break;
    case 5:  // VPACK_FLOAT16_4 DXGI_FORMAT_R16G16B16A16_FLOAT
      v = f.Unpack(v, PACK_TYPE_FLOAT16_4);
      break;
    case 6:  // VPACK_NORMPACKED64 4_20_20_20 w_z_y_x
      v = f.Unpack(v, PACK_TYPE_ULONG_4202020);
      break;
    default:
      assert_unhandled_case(type);
      return 1;
  }
  f.StoreVR(vd, v);
  return 0;
}

int InstrEmit_vxor_(PPCHIRBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // VD <- (VA) ^ (VB)
  Value* v;
  if (va == vb) {
    // Fast clear.
    v = f.LoadZeroVec128();
  } else {
    v = f.Xor(f.LoadVR(va), f.LoadVR(vb));
  }
  f.StoreVR(vd, v);
  return 0;
}
int InstrEmit_vxor(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vxor_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
int InstrEmit_vxor128(PPCHIRBuilder& f, const InstrData& i) {
  return InstrEmit_vxor_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

void RegisterEmitCategoryAltivec() {
  XEREGISTERINSTR(lvebx);
  XEREGISTERINSTR(lvehx);
  XEREGISTERINSTR(lvewx);
  XEREGISTERINSTR(lvewx128);
  XEREGISTERINSTR(lvsl);
  XEREGISTERINSTR(lvsl128);
  XEREGISTERINSTR(lvsr);
  XEREGISTERINSTR(lvsr128);
  XEREGISTERINSTR(lvx);
  XEREGISTERINSTR(lvx128);
  XEREGISTERINSTR(lvxl);
  XEREGISTERINSTR(lvxl128);
  XEREGISTERINSTR(stvebx);
  XEREGISTERINSTR(stvehx);
  XEREGISTERINSTR(stvewx);
  XEREGISTERINSTR(stvewx128);
  XEREGISTERINSTR(stvx);
  XEREGISTERINSTR(stvx128);
  XEREGISTERINSTR(stvxl);
  XEREGISTERINSTR(stvxl128);
  XEREGISTERINSTR(lvlx);
  XEREGISTERINSTR(lvlx128);
  XEREGISTERINSTR(lvlxl);
  XEREGISTERINSTR(lvlxl128);
  XEREGISTERINSTR(lvrx);
  XEREGISTERINSTR(lvrx128);
  XEREGISTERINSTR(lvrxl);
  XEREGISTERINSTR(lvrxl128);
  XEREGISTERINSTR(stvlx);
  XEREGISTERINSTR(stvlx128);
  XEREGISTERINSTR(stvlxl);
  XEREGISTERINSTR(stvlxl128);
  XEREGISTERINSTR(stvrx);
  XEREGISTERINSTR(stvrx128);
  XEREGISTERINSTR(stvrxl);
  XEREGISTERINSTR(stvrxl128);

  XEREGISTERINSTR(mfvscr);
  XEREGISTERINSTR(mtvscr);
  XEREGISTERINSTR(vaddcuw);
  XEREGISTERINSTR(vaddfp);
  XEREGISTERINSTR(vaddfp128);
  XEREGISTERINSTR(vaddsbs);
  XEREGISTERINSTR(vaddshs);
  XEREGISTERINSTR(vaddsws);
  XEREGISTERINSTR(vaddubm);
  XEREGISTERINSTR(vaddubs);
  XEREGISTERINSTR(vadduhm);
  XEREGISTERINSTR(vadduhs);
  XEREGISTERINSTR(vadduwm);
  XEREGISTERINSTR(vadduws);
  XEREGISTERINSTR(vand);
  XEREGISTERINSTR(vand128);
  XEREGISTERINSTR(vandc);
  XEREGISTERINSTR(vandc128);
  XEREGISTERINSTR(vavgsb);
  XEREGISTERINSTR(vavgsh);
  XEREGISTERINSTR(vavgsw);
  XEREGISTERINSTR(vavgub);
  XEREGISTERINSTR(vavguh);
  XEREGISTERINSTR(vavguw);
  XEREGISTERINSTR(vcfsx);
  XEREGISTERINSTR(vcsxwfp128);
  XEREGISTERINSTR(vcfpsxws128);
  XEREGISTERINSTR(vcfux);
  XEREGISTERINSTR(vcuxwfp128);
  XEREGISTERINSTR(vcfpuxws128);
  XEREGISTERINSTR(vcmpbfp);
  XEREGISTERINSTR(vcmpbfp128);
  XEREGISTERINSTR(vcmpeqfp);
  XEREGISTERINSTR(vcmpeqfp128);
  XEREGISTERINSTR(vcmpgefp);
  XEREGISTERINSTR(vcmpgefp128);
  XEREGISTERINSTR(vcmpgtfp);
  XEREGISTERINSTR(vcmpgtfp128);
  XEREGISTERINSTR(vcmpgtsb);
  XEREGISTERINSTR(vcmpgtsh);
  XEREGISTERINSTR(vcmpgtsw);
  XEREGISTERINSTR(vcmpequb);
  XEREGISTERINSTR(vcmpgtub);
  XEREGISTERINSTR(vcmpequh);
  XEREGISTERINSTR(vcmpgtuh);
  XEREGISTERINSTR(vcmpequw);
  XEREGISTERINSTR(vcmpequw128);
  XEREGISTERINSTR(vcmpgtuw);
  XEREGISTERINSTR(vctsxs);
  XEREGISTERINSTR(vctuxs);
  XEREGISTERINSTR(vexptefp);
  XEREGISTERINSTR(vexptefp128);
  XEREGISTERINSTR(vlogefp);
  XEREGISTERINSTR(vlogefp128);
  XEREGISTERINSTR(vmaddfp);
  XEREGISTERINSTR(vmaddfp128);
  XEREGISTERINSTR(vmaddcfp128);
  XEREGISTERINSTR(vmaxfp);
  XEREGISTERINSTR(vmaxfp128);
  XEREGISTERINSTR(vmaxsb);
  XEREGISTERINSTR(vmaxsh);
  XEREGISTERINSTR(vmaxsw);
  XEREGISTERINSTR(vmaxub);
  XEREGISTERINSTR(vmaxuh);
  XEREGISTERINSTR(vmaxuw);
  XEREGISTERINSTR(vmhaddshs);
  XEREGISTERINSTR(vmhraddshs);
  XEREGISTERINSTR(vminfp);
  XEREGISTERINSTR(vminfp128);
  XEREGISTERINSTR(vminsb);
  XEREGISTERINSTR(vminsh);
  XEREGISTERINSTR(vminsw);
  XEREGISTERINSTR(vminub);
  XEREGISTERINSTR(vminuh);
  XEREGISTERINSTR(vminuw);
  XEREGISTERINSTR(vmladduhm);
  XEREGISTERINSTR(vmrghb);
  XEREGISTERINSTR(vmrghh);
  XEREGISTERINSTR(vmrghw);
  XEREGISTERINSTR(vmrghw128);
  XEREGISTERINSTR(vmrglb);
  XEREGISTERINSTR(vmrglh);
  XEREGISTERINSTR(vmrglw);
  XEREGISTERINSTR(vmrglw128);
  XEREGISTERINSTR(vmsummbm);
  XEREGISTERINSTR(vmsumshm);
  XEREGISTERINSTR(vmsumshs);
  XEREGISTERINSTR(vmsumubm);
  XEREGISTERINSTR(vmsumuhm);
  XEREGISTERINSTR(vmsumuhs);
  XEREGISTERINSTR(vmsum3fp128);
  XEREGISTERINSTR(vmsum4fp128);
  XEREGISTERINSTR(vmulesb);
  XEREGISTERINSTR(vmulesh);
  XEREGISTERINSTR(vmuleub);
  XEREGISTERINSTR(vmuleuh);
  XEREGISTERINSTR(vmulosb);
  XEREGISTERINSTR(vmulosh);
  XEREGISTERINSTR(vmuloub);
  XEREGISTERINSTR(vmulouh);
  XEREGISTERINSTR(vmulfp128);
  XEREGISTERINSTR(vnmsubfp);
  XEREGISTERINSTR(vnmsubfp128);
  XEREGISTERINSTR(vnor);
  XEREGISTERINSTR(vnor128);
  XEREGISTERINSTR(vor);
  XEREGISTERINSTR(vor128);
  XEREGISTERINSTR(vperm);
  XEREGISTERINSTR(vperm128);
  XEREGISTERINSTR(vpermwi128);
  XEREGISTERINSTR(vpkpx);
  XEREGISTERINSTR(vpkshss);
  XEREGISTERINSTR(vpkshss128);
  XEREGISTERINSTR(vpkshus);
  XEREGISTERINSTR(vpkshus128);
  XEREGISTERINSTR(vpkswss);
  XEREGISTERINSTR(vpkswss128);
  XEREGISTERINSTR(vpkswus);
  XEREGISTERINSTR(vpkswus128);
  XEREGISTERINSTR(vpkuhum);
  XEREGISTERINSTR(vpkuhum128);
  XEREGISTERINSTR(vpkuhus);
  XEREGISTERINSTR(vpkuhus128);
  XEREGISTERINSTR(vpkuwum);
  XEREGISTERINSTR(vpkuwum128);
  XEREGISTERINSTR(vpkuwus);
  XEREGISTERINSTR(vpkuwus128);
  XEREGISTERINSTR(vpkd3d128);
  XEREGISTERINSTR(vrefp);
  XEREGISTERINSTR(vrefp128);
  XEREGISTERINSTR(vrfim);
  XEREGISTERINSTR(vrfim128);
  XEREGISTERINSTR(vrfin);
  XEREGISTERINSTR(vrfin128);
  XEREGISTERINSTR(vrfip);
  XEREGISTERINSTR(vrfip128);
  XEREGISTERINSTR(vrfiz);
  XEREGISTERINSTR(vrfiz128);
  XEREGISTERINSTR(vrlb);
  XEREGISTERINSTR(vrlh);
  XEREGISTERINSTR(vrlw);
  XEREGISTERINSTR(vrlw128);
  XEREGISTERINSTR(vrlimi128);
  XEREGISTERINSTR(vrsqrtefp);
  XEREGISTERINSTR(vrsqrtefp128);
  XEREGISTERINSTR(vsel);
  XEREGISTERINSTR(vsel128);
  XEREGISTERINSTR(vsl);
  XEREGISTERINSTR(vslb);
  XEREGISTERINSTR(vslh);
  XEREGISTERINSTR(vslo);
  XEREGISTERINSTR(vslo128);
  XEREGISTERINSTR(vslw);
  XEREGISTERINSTR(vslw128);
  XEREGISTERINSTR(vsldoi);
  XEREGISTERINSTR(vsldoi128);
  XEREGISTERINSTR(vspltb);
  XEREGISTERINSTR(vsplth);
  XEREGISTERINSTR(vspltw);
  XEREGISTERINSTR(vspltw128);
  XEREGISTERINSTR(vspltisb);
  XEREGISTERINSTR(vspltish);
  XEREGISTERINSTR(vspltisw);
  XEREGISTERINSTR(vspltisw128);
  XEREGISTERINSTR(vsr);
  XEREGISTERINSTR(vsrab);
  XEREGISTERINSTR(vsrah);
  XEREGISTERINSTR(vsraw);
  XEREGISTERINSTR(vsraw128);
  XEREGISTERINSTR(vsrb);
  XEREGISTERINSTR(vsrh);
  XEREGISTERINSTR(vsro);
  XEREGISTERINSTR(vsro128);
  XEREGISTERINSTR(vsrw);
  XEREGISTERINSTR(vsrw128);
  XEREGISTERINSTR(vsubcuw);
  XEREGISTERINSTR(vsubfp);
  XEREGISTERINSTR(vsubfp128);
  XEREGISTERINSTR(vsubsbs);
  XEREGISTERINSTR(vsubshs);
  XEREGISTERINSTR(vsubsws);
  XEREGISTERINSTR(vsububm);
  XEREGISTERINSTR(vsubuhm);
  XEREGISTERINSTR(vsubuwm);
  XEREGISTERINSTR(vsububs);
  XEREGISTERINSTR(vsubuhs);
  XEREGISTERINSTR(vsubuws);
  XEREGISTERINSTR(vsumsws);
  XEREGISTERINSTR(vsum2sws);
  XEREGISTERINSTR(vsum4sbs);
  XEREGISTERINSTR(vsum4shs);
  XEREGISTERINSTR(vsum4ubs);
  XEREGISTERINSTR(vupkhpx);
  XEREGISTERINSTR(vupkhsb);
  XEREGISTERINSTR(vupkhsb128);
  XEREGISTERINSTR(vupkhsh);
  XEREGISTERINSTR(vupklpx);
  XEREGISTERINSTR(vupklsb);
  XEREGISTERINSTR(vupklsb128);
  XEREGISTERINSTR(vupklsh);
  XEREGISTERINSTR(vupkd3d128);
  XEREGISTERINSTR(vxor);
  XEREGISTERINSTR(vxor128);
}

}  // namespace ppc
}  // namespace cpu
}  // namespace xe
