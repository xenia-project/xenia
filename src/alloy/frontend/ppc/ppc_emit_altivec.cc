/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/ppc/ppc_emit-private.h>

#include <alloy/frontend/ppc/ppc_context.h>
#include <alloy/frontend/ppc/ppc_function_builder.h>


using namespace alloy::frontend::ppc;
using namespace alloy::hir;
using namespace alloy::runtime;


namespace alloy {
namespace frontend {
namespace ppc {


#define SHUFPS_SWAP_DWORDS 0x1B


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
#define VX128_5_VD128 (i.VX128_5.VD128l | (i.VX128_5.VD128h << 5))
#define VX128_5_VA128 (i.VX128_5.VA128l | (i.VX128_5.VA128h << 5))
#define VX128_5_VB128 (i.VX128_5.VB128l | (i.VX128_5.VB128h << 5))
#define VX128_5_SH    (i.VX128_5.SH)
#define VX128_R_VD128 (i.VX128_R.VD128l | (i.VX128_R.VD128h << 5))
#define VX128_R_VA128 (i.VX128_R.VA128l | (i.VX128_R.VA128h << 5) | (i.VX128_R.VA128H << 6))
#define VX128_R_VB128 (i.VX128_R.VB128l | (i.VX128_R.VB128h << 5))


// namespace {

// // Shuffle masks to shift the values over and insert zeros from the low bits.
// static __m128i __shift_table_left[16] = {
//   _mm_set_epi8(15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0), // unused
//   _mm_set_epi8(14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0, 15),
//   _mm_set_epi8(13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0, 15, 15),
//   _mm_set_epi8(12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0, 15, 15, 15),
//   _mm_set_epi8(11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0, 15, 15, 15, 15),
//   _mm_set_epi8(10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 9,  8,  7,  6,  5,  4,  3,  2,  1,  0, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 8,  7,  6,  5,  4,  3,  2,  1,  0, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 7,  6,  5,  4,  3,  2,  1,  0, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 6,  5,  4,  3,  2,  1,  0, 15, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 5,  4,  3,  2,  1,  0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 4,  3,  2,  1,  0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 3,  2,  1,  0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 2,  1,  0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 1,  0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15),
// };
// static __m128i __shift_table_right[16] = {
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0), // unused
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15, 14),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15, 14, 13),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15, 14, 13, 12),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15, 14, 13, 12, 11),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15, 14, 13, 12, 11, 10),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0, 15, 14, 13, 12, 11, 10,  9),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0, 15, 14, 13, 12, 11, 10,  9,  8),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0, 15, 14, 13, 12, 11, 10,  9,  8,  7),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6),
//   _mm_set_epi8( 0,  0,  0,  0,  0, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5),
//   _mm_set_epi8( 0,  0,  0,  0, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4),
//   _mm_set_epi8( 0,  0,  0, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3),
//   _mm_set_epi8( 0,  0, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2),
//   _mm_set_epi8( 0, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1),
// };

// }


XEEMITTER(dst,            0x7C0002AC, XDSS)(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(dstst,          0x7C0002EC, XDSS)(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(dss,            0x7C00066C, XDSS)(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lvebx,          0x7C00000E, X   )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(lvehx,          0x7C00004E, X   )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_lvewx_(PPCFunctionBuilder& f, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(lvewx,          0x7C00008E, X   )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_lvewx_(f, i, i.X.RT, i.X.RA, i.X.RB);
}
XEEMITTER(lvewx128,       VX128_1(4, 131),  VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_lvewx_(f, i, i.X.RT, i.X.RA, i.X.RB);
}

// static __m128i __lvsl_table[16] = {
//   _mm_set_epi8( 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15),
//   _mm_set_epi8( 1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16),
//   _mm_set_epi8( 2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17),
//   _mm_set_epi8( 3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18),
//   _mm_set_epi8( 4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19),
//   _mm_set_epi8( 5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20),
//   _mm_set_epi8( 6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
//   _mm_set_epi8( 7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
//   _mm_set_epi8( 8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23),
//   _mm_set_epi8( 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
//   _mm_set_epi8(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25),
//   _mm_set_epi8(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26),
//   _mm_set_epi8(12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
//   _mm_set_epi8(13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28),
//   _mm_set_epi8(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29),
//   _mm_set_epi8(15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30),
// };
// int InstrEmit_lvsl_(PPCFunctionBuilder& f, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
//   GpVar ea(c.newGpVar());
//   c.mov(ea, e.gpr_value(rb));
//   if (ra) {
//     c.add(ea, e.gpr_value(ra));
//   }
//   c.and_(ea, imm(0xF));
//   c.shl(ea, imm(4)); // table offset = (16b * sh)
//   GpVar gt(c.newGpVar());
//   c.mov(gt, imm((sysint_t)__lvsl_table));
//   XmmVar v(c.newXmmVar());
//   c.movaps(v, xmmword_ptr(gt, ea));
//   c.shufps(v, v, imm(SHUFPS_SWAP_DWORDS));
//   f.StoreVR(vd, v);
//   e.TraceVR(vd);
//   return 0;
// }
// XEEMITTER(lvsl,           0x7C00000C, X   )(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_lvsl_(f, i, i.X.RT, i.X.RA, i.X.RB);
// }
// XEEMITTER(lvsl128,        VX128_1(4, 3),    VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_lvsl_(f, i, i.X.RT, i.X.RA, i.X.RB);
// }

// static __m128i __lvsr_table[16] = {
//   _mm_set_epi8(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31),
//   _mm_set_epi8(15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30),
//   _mm_set_epi8(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29),
//   _mm_set_epi8(13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28),
//   _mm_set_epi8(12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
//   _mm_set_epi8(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26),
//   _mm_set_epi8(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25),
//   _mm_set_epi8( 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
//   _mm_set_epi8( 8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23),
//   _mm_set_epi8( 7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
//   _mm_set_epi8( 6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
//   _mm_set_epi8( 5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20),
//   _mm_set_epi8( 4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19),
//   _mm_set_epi8( 3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18),
//   _mm_set_epi8( 2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17),
//   _mm_set_epi8( 1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16),
// };
// int InstrEmit_lvsr_(PPCFunctionBuilder& f, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
//   GpVar ea(c.newGpVar());
//   c.mov(ea, e.gpr_value(rb));
//   if (ra) {
//     c.add(ea, e.gpr_value(ra));
//   }
//   c.and_(ea, imm(0xF));
//   c.shl(ea, imm(4)); // table offset = (16b * sh)
//   GpVar gt(c.newGpVar());
//   c.mov(gt, imm((sysint_t)__lvsr_table));
//   XmmVar v(c.newXmmVar());
//   c.movaps(v, xmmword_ptr(gt, ea));
//   c.shufps(v, v, imm(SHUFPS_SWAP_DWORDS));
//   f.StoreVR(vd, v);
//   e.TraceVR(vd);
//   return 0;
// }
// XEEMITTER(lvsr,           0x7C00004C, X   )(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_lvsr_(f, i, i.X.RT, i.X.RA, i.X.RB);
// }
// XEEMITTER(lvsr128,        VX128_1(4, 67),   VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_lvsr_(f, i, i.X.RT, i.X.RA, i.X.RB);
// }

int InstrEmit_lvx_(PPCFunctionBuilder& f, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
  Value* ea = ra ? f.Add(f.LoadGPR(ra), f.LoadGPR(rb)) : f.LoadGPR(rb);
  f.StoreVR(vd, f.ByteSwap(f.Load(ea, VEC128_TYPE)));
  return 0;
}
XEEMITTER(lvx,            0x7C0000CE, X   )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_lvx_(f, i, i.X.RT, i.X.RA, i.X.RB);
}
XEEMITTER(lvx128,         VX128_1(4, 195),  VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_lvx_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}
XEEMITTER(lvxl,           0x7C0002CE, X   )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_lvx(f, i);
}
XEEMITTER(lvxl128,        VX128_1(4, 707),  VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_lvx128(f, i);
}

XEEMITTER(stvebx,         0x7C00010E, X   )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(stvehx,         0x7C00014E, X   )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_stvewx_(PPCFunctionBuilder& f, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(stvewx,         0x7C00018E, X   )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_stvewx_(f, i, i.X.RT, i.X.RA, i.X.RB);
}
XEEMITTER(stvewx128,      VX128_1(4, 387),  VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_stvewx_(f, i, i.X.RT, i.X.RA, i.X.RB);
}

int InstrEmit_stvx_(PPCFunctionBuilder& f, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
  Value* ea = ra ? f.Add(f.LoadGPR(ra), f.LoadGPR(rb)) : f.LoadGPR(rb);
  f.Store(ea, f.ByteSwap(f.LoadVR(vd)));
  return 0;
}
XEEMITTER(stvx,           0x7C0001CE, X   )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_stvx_(f, i, i.X.RT, i.X.RA, i.X.RB);
}
XEEMITTER(stvx128,        VX128_1(4, 451),  VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_stvx_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
}
XEEMITTER(stvxl,          0x7C0003CE, X   )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_stvx(f, i);
}
XEEMITTER(stvxl128,       VX128_1(4, 963),  VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_stvx128(f, i);
}

// // The lvlx/lvrx/etc instructions are in Cell docs only:
// // https://www-01.ibm.com/chips/techlib/techlib.nsf/techdocs/C40E4C6133B31EE8872570B500791108/$file/vector_simd_pem_v_2.07c_26Oct2006_cell.pdf
// int InstrEmit_lvlx_(PPCFunctionBuilder& f, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
//   GpVar ea(c.newGpVar());
//   c.mov(ea, e.gpr_value(rb));
//   if (ra) {
//     c.add(ea, e.gpr_value(ra));
//   }
//   GpVar sh(c.newGpVar());
//   c.mov(sh, ea);
//   c.and_(sh, imm(0xF));
//   XmmVar v = e.ReadMemoryXmm(i.address, ea, 4);
//   // If fully aligned skip complex work.
//   Label done(c.newLabel());
//   c.test(sh, sh);
//   c.jz(done);
//   {
//     // Shift left by the number of bytes offset and fill with zeros.
//     // We reuse the lvsl table here, as it does that for us.
//     GpVar gt(c.newGpVar());
//     c.xor_(gt, gt);
//     c.pinsrb(v, gt.r8(), imm(15));
//     c.shl(sh, imm(4)); // table offset = (16b * sh)
//     c.mov(gt, imm((sysint_t)__shift_table_left));
//     c.pshufb(v, xmmword_ptr(gt, sh));
//   }
//   c.bind(done);
//   c.shufps(v, v, imm(SHUFPS_SWAP_DWORDS));
//   f.StoreVR(vd, v);
//   e.TraceVR(vd);
//   return 0;
// }
// XEEMITTER(lvlx,           0x7C00040E, X   )(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_lvlx_(f, i, i.X.RT, i.X.RA, i.X.RB);
// }
// XEEMITTER(lvlx128,        VX128_1(4, 1027), VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_lvlx_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
// }
// XEEMITTER(lvlxl,          0x7C00060E, X   )(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_lvlx(f, i);
// }
// XEEMITTER(lvlxl128,       VX128_1(4, 1539), VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_lvlx128(f, i);
// }

// int InstrEmit_lvrx_(PPCFunctionBuilder& f, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
//   GpVar ea(c.newGpVar());
//   c.mov(ea, e.gpr_value(rb));
//   if (ra) {
//     c.add(ea, e.gpr_value(ra));
//   }
//   GpVar sh(c.newGpVar());
//   c.mov(sh, ea);
//   c.and_(sh, imm(0xF));
//   // If fully aligned skip complex work.
//   XmmVar v(c.newXmmVar());
//   c.pxor(v, v);
//   Label done(c.newLabel());
//   c.test(sh, sh);
//   c.jz(done);
//   {
//     // Shift left by the number of bytes offset and fill with zeros.
//     // We reuse the lvsl table here, as it does that for us.
//     c.movaps(v, e.ReadMemoryXmm(i.address, ea, 4));
//     GpVar gt(c.newGpVar());
//     c.xor_(gt, gt);
//     c.pinsrb(v, gt.r8(), imm(0));
//     c.shl(sh, imm(4)); // table offset = (16b * sh)
//     c.mov(gt, imm((sysint_t)__shift_table_right));
//     c.pshufb(v, xmmword_ptr(gt, sh));
//     c.shufps(v, v, imm(SHUFPS_SWAP_DWORDS));
//   }
//   c.bind(done);
//   f.StoreVR(vd, v);
//   e.TraceVR(vd);
//   return 0;
// }
// XEEMITTER(lvrx,           0x7C00044E, X   )(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_lvrx_(f, i, i.X.RT, i.X.RA, i.X.RB);
// }
// XEEMITTER(lvrx128,        VX128_1(4, 1091), VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_lvrx_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
// }
// XEEMITTER(lvrxl,          0x7C00064E, X   )(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_lvrx(f, i);
// }
// XEEMITTER(lvrxl128,       VX128_1(4, 1603), VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_lvrx128(f, i);
// }

// // TODO(benvanik): implement for real - this is in the memcpy path.
// static void __emulated_stvlx(uint64_t addr, __m128i vd) {
//   // addr here is the fully translated address.
//   const uint8_t eb = addr & 0xF;
//   const size_t size = 16 - eb;
//   uint8_t* p = (uint8_t*)addr;
//   for (size_t i = 0; i < size; i++) {
//     p[i] = vd.m128i_u8[15 - i];
//   }
// }
// int InstrEmit_stvlx_(PPCFunctionBuilder& f, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
//   GpVar ea(c.newGpVar());
//   c.mov(ea, e.gpr_value(rb));
//   if (ra) {
//     c.add(ea, e.gpr_value(ra));
//   }
//   ea = e.TouchMemoryAddress(i.address, ea);
//   XmmVar tvd(c.newXmmVar());
//   c.movaps(tvd, f.LoadVR(vd));
//   c.shufps(tvd, tvd, imm(SHUFPS_SWAP_DWORDS));
//   c.save(tvd);
//   GpVar pvd(c.newGpVar());
//   c.lea(pvd, tvd.m128());
//   X86CompilerFuncCall* call = c.call(__emulated_stvlx);
//   uint32_t args[] = {kX86VarTypeGpq, kX86VarTypeGpq};
//   call->setPrototype(kX86FuncConvDefault, kX86VarTypeGpq, args, XECOUNT(args));
//   call->setArgument(0, ea);
//   call->setArgument(1, pvd);
//   e.TraceVR(vd);
//   return 0;
// }
// XEEMITTER(stvlx,          0x7C00050E, X   )(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_stvlx_(f, i, i.X.RT, i.X.RA, i.X.RB);
// }
// XEEMITTER(stvlx128,       VX128_1(4, 1283), VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_stvlx_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
// }
// XEEMITTER(stvlxl,         0x7C00070E, X   )(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_stvlx(f, i);
// }
// XEEMITTER(stvlxl128,      VX128_1(4, 1795), VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_stvlx128(f, i);
// }

// // TODO(benvanik): implement for real - this is in the memcpy path.
// static void __emulated_stvrx(uint64_t addr, __m128i vd) {
//   // addr here is the fully translated address.
//   const uint8_t eb = addr & 0xF;
//   const size_t size = eb;
//   addr &= ~0xF;
//   uint8_t* p = (uint8_t*)addr;
//   // Note that if the input is already 16b aligned no bytes are stored.
//   for (size_t i = 0; i < size; i++) {
//     p[size - 1 - i] = vd.m128i_u8[i];
//   }
// }
// int InstrEmit_stvrx_(PPCFunctionBuilder& f, InstrData& i, uint32_t vd, uint32_t ra, uint32_t rb) {
//   GpVar ea(c.newGpVar());
//   c.mov(ea, e.gpr_value(rb));
//   if (ra) {
//     c.add(ea, e.gpr_value(ra));
//   }
//   ea = e.TouchMemoryAddress(i.address, ea);
//   XmmVar tvd(c.newXmmVar());
//   c.movaps(tvd, f.LoadVR(vd));
//   c.shufps(tvd, tvd, imm(SHUFPS_SWAP_DWORDS));
//   c.save(tvd);
//   GpVar pvd(c.newGpVar());
//   c.lea(pvd, tvd.m128());
//   X86CompilerFuncCall* call = c.call(__emulated_stvrx);
//   uint32_t args[] = {kX86VarTypeGpq, kX86VarTypeGpq};
//   call->setPrototype(kX86FuncConvDefault, kX86VarTypeGpq, args, XECOUNT(args));
//   call->setArgument(0, ea);
//   call->setArgument(1, pvd);
//   e.TraceVR(vd);
//   return 0;
// }
// XEEMITTER(stvrx,          0x7C00054E, X   )(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_stvrx_(f, i, i.X.RT, i.X.RA, i.X.RB);
// }
// XEEMITTER(stvrx128,       VX128_1(4, 1347), VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_stvrx_(f, i, VX128_1_VD128, i.VX128_1.RA, i.VX128_1.RB);
// }
// XEEMITTER(stvrxl,         0x7C00074E, X   )(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_stvrx(f, i);
// }
// XEEMITTER(stvrxl128,      VX128_1(4, 1859), VX128_1)(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_stvrx128(f, i);
// }

XEEMITTER(mfvscr,         0x10000604, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(mtvscr,         0x10000644, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vaddcuw,        0x10000180, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vaddfp_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD) <- (VA) + (VB) (4 x fp)
  Value* v = f.Add(f.LoadVR(va), f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vaddfp,         0x1000000A, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vaddfp_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vaddfp128,      VX128(5, 16),     VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vaddfp_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

XEEMITTER(vaddsbs,        0x10000300, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vaddshs,        0x10000340, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vaddsws,        0x10000380, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vaddubm,        0x10000000, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vaddubs,        0x10000200, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vadduhm,        0x10000040, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vadduhs,        0x10000240, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vadduwm,        0x10000080, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vadduws,        0x10000280, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vand_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // VD <- (VA) & (VB)
  Value* v = f.And(f.LoadVR(va), f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vand,           0x10000404, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vand_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vand128,        VX128(5, 528),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vand_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vandc_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // VD <- (VA) & ¬(VB)
  Value* v = f.And(f.LoadVR(va), f.Not(f.LoadVR(vb)));
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vandc,          0x10000444, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vandc_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vandc128,       VX128(5, 592),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vandc_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

XEEMITTER(vavgsb,         0x10000502, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vavgsh,         0x10000542, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vavgsw,         0x10000582, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vavgub,         0x10000402, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vavguh,         0x10000442, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vavguw,         0x10000482, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcfsx,          0x1000034A, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcsxwfp128,     VX128_3(6, 688),  VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  // (VD) <- float(VB) / 2^uimm
  uint32_t uimm = VX128_3_IMM;
  uimm = uimm ? (2 << (uimm - 1)) : 1;
  Value* v = f.Div(
      f.VectorConvertI2F(f.LoadVR(VX128_3_VB128)),
      f.Splat(f.LoadConstant((float)uimm), VEC128_TYPE));
  f.StoreVR(VX128_3_VD128, v);
  return 0;
}

XEEMITTER(vcfpsxws128,    VX128_3(6, 560),  VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcfux,          0x1000030A, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcuxwfp128,     VX128_3(6, 752),  VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vcfpuxws128,    VX128_3(6, 624),  VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vcmpbfp_(PPCFunctionBuilder& f, InstrData& i, uint32_t vd, uint32_t va, uint32_t vb, uint32_t rc) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vcmpbfp,        0x100003C6, VXR )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpbfp_(f, i, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpbfp128,     VX128(6, 384),    VX128_R)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpbfp_(f, i, VX128_R_VD128, VX128_R_VA128, VX128_R_VB128, i.VX128_R.Rc);
}

enum vcmpxxfp_op {
  vcmpxxfp_eq,
  vcmpxxfp_gt,
  vcmpxxfp_ge,
};
int InstrEmit_vcmpxxfp_(PPCFunctionBuilder& f, InstrData& i, vcmpxxfp_op cmpop, uint32_t vd, uint32_t va, uint32_t vb, uint32_t rc) {
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
    XEASSERTALWAYS();
    break;
  }
  if (rc) {
    f.UpdateCR6(v);
  }
  f.StoreVR(vd, v);
  return 0;
}

XEEMITTER(vcmpeqfp,       0x100000C6, VXR )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxfp_(f, i, vcmpxxfp_eq, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpeqfp128,    VX128(6, 0),      VX128_R)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxfp_(f, i, vcmpxxfp_eq, VX128_R_VD128, VX128_R_VA128, VX128_R_VB128, i.VX128_R.Rc);
}
XEEMITTER(vcmpgefp,       0x100001C6, VXR )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxfp_(f, i, vcmpxxfp_ge, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpgefp128,    VX128(6, 128),    VX128_R)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxfp_(f, i, vcmpxxfp_ge, VX128_R_VD128, VX128_R_VA128, VX128_R_VB128, i.VX128_R.Rc);
}
XEEMITTER(vcmpgtfp,       0x100002C6, VXR )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxfp_(f, i, vcmpxxfp_gt, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpgtfp128,    VX128(6, 256),    VX128_R)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxfp_(f, i, vcmpxxfp_gt, VX128_R_VD128, VX128_R_VA128, VX128_R_VB128, i.VX128_R.Rc);
}

enum vcmpxxi_op {
  vcmpxxi_eq,
  vcmpxxi_gt_signed,
  vcmpxxi_gt_unsigned,
};
int InstrEmit_vcmpxxi_(PPCFunctionBuilder& f, InstrData& i, vcmpxxi_op cmpop, uint32_t width, uint32_t vd, uint32_t va, uint32_t vb, uint32_t rc) {
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
    default: XEASSERTALWAYS(); return 1;
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
    default: XEASSERTALWAYS(); return 1;
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
    default: XEASSERTALWAYS(); return 1;
    }
    break;
  default: XEASSERTALWAYS(); return 1;
  }
  if (rc) {
    f.UpdateCR6(v);
  }
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vcmpequb,       0x10000006, VXR )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_eq, 1, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpequh,       0x10000046, VXR )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_eq, 2, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpequw,       0x10000086, VXR )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_eq, 4, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpequw128,    VX128(6, 512),    VX128_R)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_eq, 4, VX128_R_VD128, VX128_R_VA128, VX128_R_VB128, i.VX128_R.Rc);
}
XEEMITTER(vcmpgtsb,       0x10000306, VXR )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_gt_signed, 1, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpgtsh,       0x10000346, VXR )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_gt_signed, 2, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpgtsw,       0x10000386, VXR )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_gt_signed, 4, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpgtub,       0x10000206, VXR )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_gt_unsigned, 1, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpgtuh,       0x10000246, VXR )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_gt_unsigned, 2, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}
XEEMITTER(vcmpgtuw,       0x10000286, VXR )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vcmpxxi_(f, i, vcmpxxi_gt_unsigned, 4, i.VXR.VD, i.VXR.VA, i.VXR.VB, i.VXR.Rc);
}

XEEMITTER(vctsxs,         0x100003CA, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vctuxs,         0x1000038A, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vexptefp,       0x1000018A, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vexptefp128,    VX128_3(6, 1712), VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vlogefp,        0x100001CA, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vlogefp128,     VX128_3(6, 1776), VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmaddfp_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc) {
  // (VD) <- ((VA) * (VC)) + (VB)
  Value* v = f.MulAdd(
      f.LoadVR(va), f.LoadVR(vc), f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vmaddfp,        0x1000002E, VXA )(PPCFunctionBuilder& f, InstrData& i) {
  // (VD) <- ((VA) * (VC)) + (VB)
  return InstrEmit_vmaddfp_(f, i.VXA.VD, i.VXA.VA, i.VXA.VB, i.VXA.VC);
}
XEEMITTER(vmaddfp128,     VX128(5, 208),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  // (VD) <- ((VA) * (VB)) + (VD)
  // NOTE: this resuses VD and swaps the arg order!
  return InstrEmit_vmaddfp_(f, VX128_VD128, VX128_VA128, VX128_VD128, VX128_VB128);
}

XEEMITTER(vmaddcfp128,    VX128(5, 272),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  // (VD) <- ((VA) * (VD)) + (VB)
  Value* v = f.MulAdd(
      f.LoadVR(VX128_VA128), f.LoadVR(VX128_VD128), f.LoadVR(VX128_VB128));
  f.StoreVR(VX128_VD128, v);
  return 0;
}

int InstrEmit_vmaxfp_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD) <- max((VA), (VB))
  Value* v = f.Max(f.LoadVR(va), f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vmaxfp,         0x1000040A, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vmaxfp_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vmaxfp128,      VX128(6, 640),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vmaxfp_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

XEEMITTER(vmaxsb,         0x10000102, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmaxsh,         0x10000142, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmaxsw,         0x10000182, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmaxub,         0x10000002, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmaxuh,         0x10000042, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmaxuw,         0x10000082, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmhaddshs,      0x10000020, VXA )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmhraddshs,     0x10000021, VXA )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vminfp_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD) <- min((VA), (VB))
  Value* v = f.Min(f.LoadVR(va), f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vminfp,         0x1000044A, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vminfp_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vminfp128,      VX128(6, 704),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vminfp_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

XEEMITTER(vminsb,         0x10000302, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vminsh,         0x10000342, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vminsw,         0x10000382, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vminub,         0x10000202, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vminuh,         0x10000242, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vminuw,         0x10000282, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmladduhm,      0x10000022, VXA )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmrghb,         0x1000000C, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmrghh,         0x1000004C, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmrghw_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD.x) = (VA.x)
  // (VD.y) = (VB.x)
  // (VD.z) = (VA.y)
  // (VD.w) = (VB.y)
  Value* v = f.Permute(
      f.LoadConstant(0x05010400),
      f.LoadVR(va),
      f.LoadVR(vb),
      INT32_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vmrghw,         0x1000008C, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vmrghw_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vmrghw128,      VX128(6, 768),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vmrghw_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

XEEMITTER(vmrglb,         0x1000010C, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmrglh,         0x1000014C, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vmrglw_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD.x) = (VA.z)
  // (VD.y) = (VB.z)
  // (VD.z) = (VA.w)
  // (VD.w) = (VB.w)
  Value* v = f.Permute(
      f.LoadConstant(0x07030602),
      f.LoadVR(va),
      f.LoadVR(vb),
      INT32_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vmrglw,         0x1000018C, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vmrglw_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vmrglw128,      VX128(6, 832),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vmrglw_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

XEEMITTER(vmsummbm,       0x10000025, VXA )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmsumshm,       0x10000028, VXA )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmsumshs,       0x10000029, VXA )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmsumubm,       0x10000024, VXA )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmsumuhm,       0x10000026, VXA )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmsumuhs,       0x10000027, VXA )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmsum3fp128,    VX128(5, 400),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  // Dot product XYZ.
  // (VD.xyzw) = (VA.x * VB.x) + (VA.y * VB.y) + (VA.z * VB.z)
  Value* v = f.DotProduct3(f.LoadVR(VX128_VA128), f.LoadVR(VX128_VB128));
  v = f.Splat(v, VEC128_TYPE);
  f.StoreVR(VX128_VD128, v);
  return 0;
}

XEEMITTER(vmsum4fp128,    VX128(5, 464),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  // Dot product XYZW.
  // (VD.xyzw) = (VA.x * VB.x) + (VA.y * VB.y) + (VA.z * VB.z) + (VA.w * VB.w)
  Value* v = f.DotProduct4(f.LoadVR(VX128_VA128), f.LoadVR(VX128_VB128));
  v = f.Splat(v, VEC128_TYPE);
  f.StoreVR(VX128_VD128, v);
  return 0;
}

XEEMITTER(vmulesb,        0x10000308, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmulesh,        0x10000348, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmuleub,        0x10000208, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmuleuh,        0x10000248, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmulosb,        0x10000108, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmulosh,        0x10000148, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmuloub,        0x10000008, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmulouh,        0x10000048, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vmulfp128,      VX128(5, 144),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  // (VD) <- (VA) * (VB) (4 x fp)
  Value* v = f.Mul(f.LoadVR(VX128_VA128), f.LoadVR(VX128_VB128));
  f.StoreVR(VX128_VD128, v);
  return 0;
}

int InstrEmit_vnmsubfp_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc) {
  // (VD) <- -(((VA) * (VC)) - (VB))
  // NOTE: only one rounding should take place, but that's hard...
  // This really needs VFNMSUB132PS/VFNMSUB213PS/VFNMSUB231PS but that's AVX.
  Value* v = f.Neg(f.MulSub(f.LoadVR(va), f.LoadVR(vc), f.LoadVR(vb)));
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vnmsubfp,       0x1000002F, VXA )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vnmsubfp_(f, i.VXA.VD, i.VXA.VA, i.VXA.VB, i.VXA.VC);
}
XEEMITTER(vnmsubfp128,    VX128(5, 336),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vnmsubfp_(f, VX128_VD128, VX128_VA128, VX128_VB128, VX128_VD128);
}

int InstrEmit_vnor_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // VD <- ¬((VA) | (VB))
  Value* v = f.Not(f.Or(f.LoadVR(va), f.LoadVR(vb)));
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vnor,           0x10000504, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vnor_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vnor128,        VX128(5, 656),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vnor_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vor_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
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
XEEMITTER(vor,            0x10000484, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vor_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vor128,         VX128(5, 720),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vor_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

int InstrEmit_vperm_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc) {
  Value* v = f.Permute(f.LoadVR(vc), f.LoadVR(va), f.LoadVR(vb), INT8_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vperm,          0x1000002B, VXA )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vperm_(f, i.VXA.VD, i.VXA.VA, i.VXA.VB, i.VXA.VC);
}
XEEMITTER(vperm128,       VX128_2(5, 0),    VX128_2)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vperm_(f, VX128_2_VD128, VX128_2_VA128, VX128_2_VB128, VX128_2_VC);
}

XEEMITTER(vpermwi128,     VX128_P(6, 528),  VX128_P)(PPCFunctionBuilder& f, InstrData& i) {
  // (VD.x) = (VB.uimm[6-7])
  // (VD.y) = (VB.uimm[4-5])
  // (VD.z) = (VB.uimm[2-3])
  // (VD.w) = (VB.uimm[0-1])
  const uint32_t vd = i.VX128_P.VD128l | (i.VX128_P.VD128h << 5);
  const uint32_t vb = i.VX128_P.VB128l | (i.VX128_P.VB128h << 5);
  uint32_t uimm = i.VX128_P.PERMl | (i.VX128_P.PERMh << 5);
  Value* v = f.Swizzle(f.LoadVR(vb), INT32_TYPE, uimm);
  f.StoreVR(vd, v);
  return 0;
}

XEEMITTER(vpkpx,          0x1000030E, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkshss,        0x1000018E, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkshss128,     VX128(5, 512),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkswss,        0x100001CE, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkswss128,     VX128(5, 640),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkswus,        0x1000014E, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkswus128,     VX128(5, 704),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkuhum,        0x1000000E, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkuhum128,     VX128(5, 768),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkuhus,        0x1000008E, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkuhus128,     VX128(5, 832),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkshus,        0x1000010E, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkshus128,     VX128(5, 576),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkuwum,        0x1000004E, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkuwum128,     VX128(5, 896),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkuwus,        0x100000CE, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vpkuwus128,     VX128(5, 960),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vpkd3d128,      VX128_4(6, 1552), VX128_4)(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrefp,          0x1000010A, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vrefp128,       VX128_3(6, 1584), VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrfim,          0x100002CA, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vrfim128,       VX128_3(6, 816),  VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vrfin_(PPCFunctionBuilder& f, uint32_t vd, uint32_t vb) {
  // (VD) <- RoundToNearest(VB)
  Value* v = f.Round(f.LoadVR(vd), ROUND_TO_NEAREST);
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vrfin,          0x1000020A, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vrfin_(f, i.VX.VD, i.VX.VB);
}
XEEMITTER(vrfin128,       VX128_3(6, 880),  VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vrfin_(f, VX128_3_VD128, VX128_3_VB128);
}

XEEMITTER(vrfip,          0x1000028A, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vrfip128,       VX128_3(6, 944),  VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrfiz,          0x1000024A, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vrfiz128,       VX128_3(6, 1008), VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrlb,           0x10000004, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrlh,           0x10000044, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrlw,           0x10000084, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vrlw128,        VX128(6, 80),     VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vrlimi128,      VX128_4(6, 1808), VX128_4)(PPCFunctionBuilder& f, InstrData& i) {
  const uint32_t vd = i.VX128_4.VD128l | (i.VX128_4.VD128h << 5);
  const uint32_t vb = i.VX128_4.VB128l | (i.VX128_4.VB128h << 5);
  uint32_t blend_mask = i.VX128_4.IMM;
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
    default: XEASSERTALWAYS(); return 1;
    }
    v = f.Swizzle(f.LoadVR(vb), FLOAT32_TYPE, swizzle_mask);
  } else {
    v = f.LoadVR(vb);
  }
  v = f.Permute(
      f.LoadConstant(blend_mask), v, f.LoadVR(vd), FLOAT32_TYPE);
  f.StoreVR(vd, v);
  return 0;
}

int InstrEmit_vrsqrtefp_(PPCFunctionBuilder& f, uint32_t vd, uint32_t vb) {
  // (VD) <- 1 / sqrt(VB)
  // There are a lot of rules in the Altivec_PEM docs for handlings that
  // result in nan/infinity/etc. They are ignored here. I hope games would
  // never rely on them.
  Value* v = f.RSqrt(f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vrsqrtefp,      0x1000014A, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vrsqrtefp_(f, i.VX.VD, i.VX.VB);
}
XEEMITTER(vrsqrtefp128,   VX128_3(6, 1648), VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vrsqrtefp_(f, VX128_3_VD128, VX128_3_VB128);
}

int InstrEmit_vsel_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc) {
  Value* a = f.LoadVR(va);
  Value* v = f.Xor(f.And(f.Xor(a, f.LoadVR(vb)), f.LoadVR(vc)), a);
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vsel,           0x1000002A, VXA )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vsel_(f, i.VXA.VD, i.VXA.VA, i.VXA.VB, i.VXA.VC);
}
XEEMITTER(vsel128,        VX128(5, 848),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vsel_(f, VX128_VD128, VX128_VA128, VX128_VB128, VX128_VD128);
}

XEEMITTER(vsl,            0x100001C4, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vslb,           0x10000104, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  Value* v = f.VectorShl(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT8_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

XEEMITTER(vslh,           0x10000144, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  Value* v = f.VectorShl(f.LoadVR(i.VX.VA), f.LoadVR(i.VX.VB), INT16_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vslw_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // VA = |xxxxx|yyyyy|zzzzz|wwwww|
  // VB = |...sh|...sh|...sh|...sh|
  // VD = |x<<sh|y<<sh|z<<sh|w<<sh|
  Value* v = f.VectorShl(f.LoadVR(va), f.LoadVR(vb), INT32_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vslw,           0x10000184, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vslw_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vslw128,        VX128(6, 208),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vslw_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

// static __m128i __shift_table_out[16] = {
//   _mm_set_epi8(15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0), // unused
//   _mm_set_epi8( 0, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1),
//   _mm_set_epi8( 0,  0, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2),
//   _mm_set_epi8( 0,  0,  0, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3),
//   _mm_set_epi8( 0,  0,  0,  0, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4),
//   _mm_set_epi8( 0,  0,  0,  0,  0, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0, 15, 14, 13, 12, 11, 10,  9,  8,  7,  6),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0, 15, 14, 13, 12, 11, 10,  9,  8,  7),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0, 15, 14, 13, 12, 11, 10,  9,  8),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0, 15, 14, 13, 12, 11, 10,  9),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15, 14, 13, 12, 11, 10),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15, 14, 13, 12, 11),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15, 14, 13, 12),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15, 14, 13),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15, 14),
//   _mm_set_epi8( 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15),
// };
// static __m128i __shift_table_in[16] = {
//   _mm_set_epi8(15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15), // unused
//   _mm_set_epi8( 0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 1,  0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 2,  1,  0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 3,  2,  1,  0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 4,  3,  2,  1,  0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 5,  4,  3,  2,  1,  0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 6,  5,  4,  3,  2,  1,  0, 15, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 7,  6,  5,  4,  3,  2,  1,  0, 15, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 8,  7,  6,  5,  4,  3,  2,  1,  0, 15, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8( 9,  8,  7,  6,  5,  4,  3,  2,  1,  0, 15, 15, 15, 15, 15, 15),
//   _mm_set_epi8(10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0, 15, 15, 15, 15, 15),
//   _mm_set_epi8(11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0, 15, 15, 15, 15),
//   _mm_set_epi8(12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0, 15, 15, 15),
//   _mm_set_epi8(13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0, 15, 15),
//   _mm_set_epi8(14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0, 15),
// };
// int InstrEmit_vsldoi_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb, uint32_t sh) {
//   // (VD) <- ((VA) || (VB)) << (SH << 3)
//   if (!sh) {
//     // No shift?
//     f.StoreVR(vd, f.LoadVR(va));
//     e.TraceVR(vd, va, vb);
//     return 0;
//   } else if (sh == 16) {
//     f.StoreVR(vd, f.LoadVR(vb));
//     e.TraceVR(vd, va, vb);
//     return 0;
//   }
//   // TODO(benvanik): optimize for the rotation case:
//   // vsldoi128 vr63,vr63,vr63,4
//   // (ABCD ABCD) << 4b = (BCDA)
//   // TODO(benvanik): rewrite this piece of shit.
//   XmmVar v(c.newXmmVar());
//   c.movaps(v, f.LoadVR(va));
//   XmmVar v_r(c.newXmmVar());
//   c.movaps(v_r, f.LoadVR(vb));
//   // (VA << SH) OR (VB >> (16 - SH))
//   GpVar gt(c.newGpVar());
//   c.xor_(gt, gt);
//   c.pinsrb(v, gt.r8(), imm(0));
//   c.pinsrb(v_r, gt.r8(), imm(15));
//   c.mov(gt, imm((sysint_t)&__shift_table_out[sh]));
//   XmmVar shuf(c.newXmmVar());
//   c.movaps(shuf, xmmword_ptr(gt));
//   c.pshufb(v, shuf);
//   c.mov(gt, imm((sysint_t)&__shift_table_in[sh]));
//   c.movaps(shuf, xmmword_ptr(gt));
//   c.pshufb(v_r, shuf);
//   c.por(v, v_r);
//   f.StoreVR(vd, v);
//   e.TraceVR(vd, va, vb);
//   return 0;
// }
// XEEMITTER(vsldoi,         0x1000002C, VXA )(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_vsldoi_(f, i.VXA.VD, i.VXA.VA, i.VXA.VB, i.VXA.VC & 0xF);
// }
// XEEMITTER(vsldoi128,      VX128_5(4, 16),   VX128_5)(PPCFunctionBuilder& f, InstrData& i) {
//   return InstrEmit_vsldoi_(f, VX128_5_VD128, VX128_5_VA128, VX128_5_VB128, VX128_5_SH);
// }

XEEMITTER(vslo,           0x1000040C, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vslo128,        VX128(5, 912),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vspltb,         0x1000020C, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  // b <- UIMM*8
  // do i = 0 to 127 by 8
  //  (VD)[i:i+7] <- (VB)[b:b+7]
  Value* b = f.Extract(f.LoadVR(i.VX.VB), (i.VX.VA & 0xF), INT8_TYPE);
  Value* v = f.Splat(b, VEC128_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

XEEMITTER(vsplth,         0x1000024C, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  // (VD.xyzw) <- (VB.uimm)
  Value* h = f.Extract(f.LoadVR(i.VX.VB), (i.VX.VA & 0x7), INT16_TYPE);
  Value* v = f.Splat(h, VEC128_TYPE);
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vspltw_(PPCFunctionBuilder& f, uint32_t vd, uint32_t vb, uint32_t uimm) {
  // (VD.xyzw) <- (VB.uimm)
  Value* w = f.Extract(f.LoadVR(vb), (uimm & 0x3), INT32_TYPE);
  Value* v = f.Splat(w, VEC128_TYPE);
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vspltw,         0x1000028C, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vspltw_(f, i.VX.VD, i.VX.VB, i.VX.VA);
}
XEEMITTER(vspltw128,      VX128_3(6, 1840), VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vspltw_(f, VX128_3_VD128, VX128_3_VB128, VX128_3_IMM);
}

XEEMITTER(vspltisb,       0x1000030C, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  // (VD.xyzw) <- sign_extend(uimm)
  Value* v;
  if (i.VX.VA) {
    // Sign extend from 5bits -> 8 and load.
    int8_t simm = (i.VX.VA & 0x10) ? (i.VX.VA | 0xF0) : i.VX.VA;
    v = f.Splat(f.LoadConstant(simm), VEC128_TYPE);
  } else {
    // Zero out the register.
    v = f.LoadZero(VEC128_TYPE);
  }
  f.StoreVR(i.VX.VD, v);
  return 0;
}

XEEMITTER(vspltish,       0x1000034C, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  // (VD.xyzw) <- sign_extend(uimm)
  Value* v;
  if (i.VX.VA) {
    // Sign extend from 5bits -> 16 and load.
    int16_t simm = (i.VX.VA & 0x10) ? (i.VX.VA | 0xFFF0) : i.VX.VA;
    v = f.Splat(f.LoadConstant(simm), VEC128_TYPE);
  } else {
    // Zero out the register.
    v = f.LoadZero(VEC128_TYPE);
  }
  f.StoreVR(i.VX.VD, v);
  return 0;
}

int InstrEmit_vspltisw_(PPCFunctionBuilder& f, uint32_t vd, uint32_t uimm) {
  // (VD.xyzw) <- sign_extend(uimm)
  Value* v;
  if (uimm) {
    // Sign extend from 5bits -> 32 and load.
    int32_t simm = (uimm & 0x10) ? (uimm | 0xFFFFFFF0) : uimm;
    v = f.Splat(f.LoadConstant(simm), VEC128_TYPE);
  } else {
    // Zero out the register.
    v = f.LoadZero(VEC128_TYPE);
  }
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vspltisw,       0x1000038C, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vspltisw_(f, i.VX.VD, i.VX.VA);
}
XEEMITTER(vspltisw128,    VX128_3(6, 1904), VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vspltisw_(f, VX128_3_VD128, VX128_3_IMM);
}

XEEMITTER(vsr,            0x100002C4, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsrab,          0x10000304, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsrah,          0x10000344, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsraw,          0x10000384, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vsraw128,       VX128(6, 336),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsrb,           0x10000204, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsrh,           0x10000244, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsro,           0x1000044C, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vsro128,        VX128(5, 976),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsrw,           0x10000284, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vsrw128,        VX128(6, 464),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubcuw,        0x10000580, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

int InstrEmit_vsubfp_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // (VD) <- (VA) - (VB) (4 x fp)
  Value* v = f.Sub(f.LoadVR(va), f.LoadVR(vb));
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vsubfp,         0x1000004A, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vsubfp_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vsubfp128,      VX128(5, 80),     VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vsubfp_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}

XEEMITTER(vsubsbs,        0x10000700, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubshs,        0x10000740, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubsws,        0x10000780, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsububm,        0x10000400, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsububs,        0x10000600, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubuhm,        0x10000440, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubuhs,        0x10000640, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubuwm,        0x10000480, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsubuws,        0x10000680, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsumsws,        0x10000788, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsum2sws,       0x10000688, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsum4sbs,       0x10000708, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsum4shs,       0x10000648, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vsum4ubs,       0x10000608, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vupkhpx,        0x1000034E, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vupkhsb,        0x1000020E, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vupkhsb128,     VX128(6, 896),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vupkhsh,        0x1000024E, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vupklpx,        0x100003CE, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vupklsb,        0x1000028E, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}
XEEMITTER(vupklsb128,     VX128(6, 960),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

XEEMITTER(vupklsh,        0x100002CE, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

// __m128 half_to_float5_SSE2(__m128i h) {
// #define SSE_CONST4(name, val) static const __declspec(align(16)) uint name[4] = { (val), (val), (val), (val) }
// #define SSE_CONST(name) *(const __m128i *)&name
// #define SSE_CONSTF(name) *(const __m128 *)&name
//     SSE_CONST4(mask_nosign,         0x7fff);
//     SSE_CONST4(magic,               (254 - 15) << 23);
//     SSE_CONST4(was_infnan,          0x7bff);
//     SSE_CONST4(exp_infnan,          255 << 23);
//     __m128i mnosign     = SSE_CONST(mask_nosign);
//     __m128i expmant     = _mm_and_si128(mnosign, h);
//     __m128i justsign    = _mm_xor_si128(h, expmant);
//     __m128i expmant2    = expmant; // copy (just here for counting purposes)
//     __m128i shifted     = _mm_slli_epi32(expmant, 13);
//     __m128  scaled      = _mm_mul_ps(_mm_castsi128_ps(shifted), *(const __m128 *)&magic);
//     __m128i b_wasinfnan = _mm_cmpgt_epi32(expmant2, SSE_CONST(was_infnan));
//     __m128i sign        = _mm_slli_epi32(justsign, 16);
//     __m128  infnanexp   = _mm_and_ps(_mm_castsi128_ps(b_wasinfnan), SSE_CONSTF(exp_infnan));
//     __m128  sign_inf    = _mm_or_ps(_mm_castsi128_ps(sign), infnanexp);
//     __m128  final       = _mm_or_ps(scaled, sign_inf);
//     // ~11 SSE2 ops.
//     return final;
// #undef SSE_CONST4
// #undef CONST
// #undef CONSTF
// }

XEEMITTER(vupkd3d128,     VX128_3(6, 2032), VX128_3)(PPCFunctionBuilder& f, InstrData& i) {
  // Can't find many docs on this. Best reference is
  // http://worldcraft.googlecode.com/svn/trunk/src/qylib/math/xmmatrix.inl,
  // which shows how it's used in some cases. Since it's all intrinsics,
  // finding it in code is pretty easy.
  const uint32_t vd = i.VX128_3.VD128l | (i.VX128_3.VD128h << 5);
  const uint32_t vb = i.VX128_3.VB128l | (i.VX128_3.VB128h << 5);
  const uint32_t type = i.VX128_3.IMM >> 2;
  Value* v;
  switch (type) {
  case 0: // VPACK_D3DCOLOR
    {
      XEASSERTALWAYS();
      return 1;
      // http://hlssmod.net/he_code/public/pixelwriter.h
      // ARGB (WXYZ) -> RGBA (XYZW)
      // zzzzZZZZzzzzARGB
      //c.movaps(vt, f.LoadVR(vb));
      //// zzzzZZZZzzzzARGB
      //// 000R000G000B000A
      //c.mov(gt, imm(
      //    ((1ull << 7) << 56) |
      //    ((1ull << 7) << 48) |
      //    ((1ull << 7) << 40) |
      //    ((0ull) << 32) | // B
      //    ((1ull << 7) << 24) |
      //    ((1ull << 7) << 16) |
      //    ((1ull << 7) << 8) |
      //    ((3ull) << 0)) // A
      //    ); // lo
      //c.movq(v, gt);
      //c.mov(gt, imm(
      //    ((1ull << 7) << 56) |
      //    ((1ull << 7) << 48) |
      //    ((1ull << 7) << 40) |
      //    ((2ull) << 32) | // R
      //    ((1ull << 7) << 24) |
      //    ((1ull << 7) << 16) |
      //    ((1ull << 7) << 8) |
      //    ((1ull) << 0)) // G
      //    ); // hi
      //c.pinsrq(v, gt, imm(1));
      //c.pshufb(vt, v);
      //// {256*R.0, 256*G.0, 256*B.0, 256*A.0}
      //c.cvtdq2ps(v, vt);
      //// {R.0, G.0, B.0 A.0}
      //// 1/256 = 0.00390625 = 0x3B800000
      //c.mov(gt, imm(0x3B800000));
      //c.movd(vt, gt.r32());
      //c.shufps(vt, vt, imm(0));
      //c.mulps(v, vt);
    }
    break;
  case 1: // VPACK_NORMSHORT2
    {
      // (VD.x) = 3.0 + (VB.x)*2^-22
      // (VD.y) = 3.0 + (VB.y)*2^-22
      // (VD.z) = 0.0
      // (VD.w) = 3.0
      // v = VB.x|VB.y|0|0
      v = f.Permute(
          f.LoadConstant(PERMUTE_XY_ZW),
          f.LoadVR(vb),
          f.LoadZero(VEC128_TYPE),
          INT32_TYPE);
      // *= 2^-22 + {3.0, 3.0, 0, 1.0}
      vec128_t v3301 = { 3.0, 3.0, 0, 1.0 };
      v = f.MulAdd(
          v,
          f.Splat(f.LoadConstant(0x34800000), VEC128_TYPE),
          f.LoadConstant(v3301));
    }
    break;
  case 3: // VPACK_... 2 FLOAT16s
    {
      // (VD.x) = fixed_16_to_32(VB.x (low))
      // (VD.y) = fixed_16_to_32(VB.x (high))
      // (VD.z) = 0.0
      // (VD.w) = 1.0
      v = f.LoadZero(VEC128_TYPE);
      f.DebugBreak();
      // 1 bit sign, 5 bit exponent, 10 bit mantissa
      // D3D10 half float format
      // TODO(benvanik): fixed_16_to_32 in SSE?
      // TODO(benvanik): http://blogs.msdn.com/b/chuckw/archive/2012/09/11/directxmath-f16c-and-fma.aspx
      // Use _mm_cvtph_ps -- requires very modern processors (SSE5+)
      // Unpacking half floats: http://fgiesen.wordpress.com/2012/03/28/half-to-float-done-quic/
      // Packing half floats: https://gist.github.com/rygorous/2156668
      // Load source, move from tight pack of X16Y16.... to X16...Y16...
      // Also zero out the high end.
      //c.int3();
      //c.movaps(vt, f.LoadVR(vb));
      //c.save(vt);
      //c.lea(gt, vt.m128());
      //X86CompilerFuncCall* call = c.call(half_to_float5_SSE2);
      //uint32_t args[] = {kX86VarTypeGpq};
      //call->setPrototype(kX86FuncConvDefault, kX86VarTypeXmm, args, XECOUNT(args));
      //call->setArgument(0, gt);
      //call->setReturn(v);
      //// Select XY00.
      //c.xorps(vt, vt);
      //c.shufps(v, vt, imm(0x04));
      //// {0.0, 0.0, 0.0, 1.0}
      //c.mov(gt, imm(0x3F800000));
      //c.pinsrd(v, gt.r32(), imm(3));
    }
    break;
  default:
    XEASSERTALWAYS();
    return 1;
  }
  f.StoreVR(vd, v);
  return 0;
}

int InstrEmit_vxor_(PPCFunctionBuilder& f, uint32_t vd, uint32_t va, uint32_t vb) {
  // VD <- (VA) ^ (VB)
  Value* v;
  if (va == vb) {
    // Fast clear.
    v = f.LoadZero(VEC128_TYPE);
  } else {
    v = f.Xor(f.LoadVR(va), f.LoadVR(vb));
  }
  f.StoreVR(vd, v);
  return 0;
}
XEEMITTER(vxor,           0x100004C4, VX  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vxor_(f, i.VX.VD, i.VX.VA, i.VX.VB);
}
XEEMITTER(vxor128,        VX128(5, 784),    VX128  )(PPCFunctionBuilder& f, InstrData& i) {
  return InstrEmit_vxor_(f, VX128_VD128, VX128_VA128, VX128_VB128);
}


void RegisterEmitCategoryAltivec() {
  XEREGISTERINSTR(dst,            0x7C0002AC);
  XEREGISTERINSTR(dstst,          0x7C0002EC);
  XEREGISTERINSTR(dss,            0x7C00066C);
  XEREGISTERINSTR(lvebx,          0x7C00000E);
  XEREGISTERINSTR(lvehx,          0x7C00004E);
  XEREGISTERINSTR(lvewx,          0x7C00008E);
  XEREGISTERINSTR(lvewx128,       VX128_1(4, 131));
  // XEREGISTERINSTR(lvsl,           0x7C00000C);
  // XEREGISTERINSTR(lvsl128,        VX128_1(4, 3));
  // XEREGISTERINSTR(lvsr,           0x7C00004C);
  // XEREGISTERINSTR(lvsr128,        VX128_1(4, 67));
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
  // XEREGISTERINSTR(lvlx,           0x7C00040E);
  // XEREGISTERINSTR(lvlx128,        VX128_1(4, 1027));
  // XEREGISTERINSTR(lvlxl,          0x7C00060E);
  // XEREGISTERINSTR(lvlxl128,       VX128_1(4, 1539));
  // XEREGISTERINSTR(lvrx,           0x7C00044E);
  // XEREGISTERINSTR(lvrx128,        VX128_1(4, 1091));
  // XEREGISTERINSTR(lvrxl,          0x7C00064E);
  // XEREGISTERINSTR(lvrxl128,       VX128_1(4, 1603));
  // XEREGISTERINSTR(stvlx,          0x7C00050E);
  // XEREGISTERINSTR(stvlx128,       VX128_1(4, 1283));
  // XEREGISTERINSTR(stvlxl,         0x7C00070E);
  // XEREGISTERINSTR(stvlxl128,      VX128_1(4, 1795));
  // XEREGISTERINSTR(stvrx,          0x7C00054E);
  // XEREGISTERINSTR(stvrx128,       VX128_1(4, 1347));
  // XEREGISTERINSTR(stvrxl,         0x7C00074E);
  // XEREGISTERINSTR(stvrxl128,      VX128_1(4, 1859));

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
  XEREGISTERINSTR(vslh,           0x10000144);
  XEREGISTERINSTR(vslo,           0x1000040C);
  XEREGISTERINSTR(vslo128,        VX128(5, 912));
  XEREGISTERINSTR(vslw,           0x10000184);
  XEREGISTERINSTR(vslw128,        VX128(6, 208));
  // XEREGISTERINSTR(vsldoi,         0x1000002C);
  // XEREGISTERINSTR(vsldoi128,      VX128_5(4, 16));
  XEREGISTERINSTR(vspltb,         0x1000020C);
  XEREGISTERINSTR(vsplth,         0x1000024C);
  XEREGISTERINSTR(vspltw,         0x1000028C);
  XEREGISTERINSTR(vspltw128,      VX128_3(6, 1840));
  XEREGISTERINSTR(vspltisb,       0x1000030C);
  XEREGISTERINSTR(vspltish,       0x1000034C);
  XEREGISTERINSTR(vspltisw,       0x1000038C);
  XEREGISTERINSTR(vspltisw128,    VX128_3(6, 1904));
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
