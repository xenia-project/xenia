/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Developers. All rights reserved.                      *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_sequences.h"

#include <algorithm>
#include <cstring>

#include "xenia/cpu/backend/a64/a64_op.h"

// For OPCODE_PACK/OPCODE_UNPACK
#include "third_party/half/include/half.hpp"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

volatile int anchor_vector = 0;

// ============================================================================
// OPCODE_VECTOR_CONVERT_I2F
// ============================================================================
struct VECTOR_CONVERT_I2F
    : Sequence<VECTOR_CONVERT_I2F,
               I<OPCODE_VECTOR_CONVERT_I2F, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // flags = ARITHMETIC_UNSIGNED
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      // Round manually to (1.stored mantissa bits * 2^31) or to 2^32 to the
      // nearest even (the only rounding mode used on AltiVec) if the number is
      // 0x80000000 or greater, instead of converting src & 0x7FFFFFFF and then
      // adding 2147483648.0f, which results in double rounding that can give a
      // result larger than needed - see OPCODE_VECTOR_CONVERT_I2F notes.

      // [0x80000000, 0xFFFFFFFF] case:

      // Round to the nearest even, from (0x80000000 | 31 stored mantissa bits)
      // to ((-1 << 23) | 23 stored mantissa bits), or to 0 if the result should
      // be 4294967296.0f.
      // xmm0 = src + 0b01111111 + ((src >> 8) & 1)
      // (xmm1 also used to launch reg + mem early and to require it late)
      // e.vpaddd(Q1, i.src1, e.GetXmmConstPtr(XMMInt127));
      // e.vpslld(Q0, i.src1, 31 - 8);
      // e.vpsrld(Q0, Q0, 31);
      // e.vpaddd(Q0, Q0, Q1);
      // xmm0 = (0xFF800000 | 23 explicit mantissa bits), or 0 if overflowed
      // e.vpsrad(Q0, Q0, 8);
      // Calculate the result for the [0x80000000, 0xFFFFFFFF] case - take the
      // rounded mantissa, and add -1 or 0 to the exponent of 32, depending on
      // whether the number should be (1.stored mantissa bits * 2^31) or 2^32.
      // xmm0 = [0x80000000, 0xFFFFFFFF] case result
      // e.vpaddd(Q0, Q0, e.GetXmmConstPtr(XMM2To32));

      // [0x00000000, 0x7FFFFFFF] case
      // (during vblendvps reg -> vpaddd reg -> vpaddd mem dependency):

      // Convert from signed integer to float.
      // xmm1 = [0x00000000, 0x7FFFFFFF] case result
      // e.vcvtdq2ps(Q1, i.src1);

      // Merge the two ways depending on whether the number is >= 0x80000000
      // (has high bit set).
      // e.vblendvps(i.dest, Q1, Q0, i.src1);
    } else {
      // e.vcvtdq2ps(i.dest, i.src1);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_CONVERT_I2F, VECTOR_CONVERT_I2F);

// ============================================================================
// OPCODE_VECTOR_CONVERT_F2I
// ============================================================================
struct VECTOR_CONVERT_F2I
    : Sequence<VECTOR_CONVERT_F2I,
               I<OPCODE_VECTOR_CONVERT_F2I, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      // clamp to min 0
      // e.vmaxps(Q0, i.src1, e.GetXmmConstPtr(XMMZero));

      // xmm1 = mask of values >= (unsigned)INT_MIN
      // e.vcmpgeps(Q1, Q0, e.GetXmmConstPtr(XMMPosIntMinPS));

      // scale any values >= (unsigned)INT_MIN back to [0, ...]
      // e.vsubps(e.xmm2, Q0, e.GetXmmConstPtr(XMMPosIntMinPS));
      // e.vblendvps(Q0, Q0, e.xmm2, Q1);

      // xmm0 = [0, INT_MAX]
      // this may still contain values > INT_MAX (if src has vals > UINT_MAX)
      // e.vcvttps2dq(i.dest, Q0);

      // xmm0 = mask of values that need saturation
      // e.vpcmpeqd(Q0, i.dest, e.GetXmmConstPtr(XMMIntMin));

      // scale values back above [INT_MIN, UINT_MAX]
      // e.vpand(Q1, Q1, e.GetXmmConstPtr(XMMIntMin));
      // e.vpaddd(i.dest, i.dest, Q1);

      // saturate values > UINT_MAX
      // e.vpor(i.dest, i.dest, Q0);
    } else {
      // xmm2 = NaN mask
      // e.vcmpunordps(e.xmm2, i.src1, i.src1);

      // convert packed floats to packed dwords
      // e.vcvttps2dq(Q0, i.src1);

      // (high bit) xmm1 = dest is indeterminate and i.src1 >= 0
      // e.vpcmpeqd(Q1, Q0, e.GetXmmConstPtr(XMMIntMin));
      // e.vpandn(Q1, i.src1, Q1);

      // saturate positive values
      // e.vblendvps(i.dest, Q0, e.GetXmmConstPtr(XMMIntMax), Q1);

      // mask NaNs
      // e.vpandn(i.dest, e.xmm2, i.dest);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_CONVERT_F2I, VECTOR_CONVERT_F2I);

// ============================================================================
// OPCODE_LOAD_VECTOR_SHL
// ============================================================================
static const vec128_t lvsl_table[16] = {
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
struct LOAD_VECTOR_SHL_I8
    : Sequence<LOAD_VECTOR_SHL_I8, I<OPCODE_LOAD_VECTOR_SHL, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      auto sh = i.src1.constant();
      assert_true(sh < xe::countof(lvsl_table));
      e.MOVP2R(X0, &lvsl_table[sh]);
      e.LDR(i.dest, X0);
    } else {
      e.MOVP2R(X0, lvsl_table);
      e.AND(X1, i.src1.reg().toX(), 0xf);
      e.LDR(i.dest, X0, X1, IndexExt::LSL, 4);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_VECTOR_SHL, LOAD_VECTOR_SHL_I8);

// ============================================================================
// OPCODE_LOAD_VECTOR_SHR
// ============================================================================
static const vec128_t lvsr_table[16] = {
    vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31),
    vec128b(15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30),
    vec128b(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29),
    vec128b(13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28),
    vec128b(12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
    vec128b(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26),
    vec128b(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25),
    vec128b(9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
    vec128b(8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23),
    vec128b(7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
    vec128b(6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
    vec128b(5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20),
    vec128b(4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19),
    vec128b(3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18),
    vec128b(2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17),
    vec128b(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16),
};
struct LOAD_VECTOR_SHR_I8
    : Sequence<LOAD_VECTOR_SHR_I8, I<OPCODE_LOAD_VECTOR_SHR, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      auto sh = i.src1.constant();
      assert_true(sh < xe::countof(lvsr_table));
      e.MOVP2R(X0, &lvsr_table[sh]);
      e.LDR(i.dest, X0);
    } else {
      e.MOVP2R(X0, lvsr_table);
      e.AND(X1, i.src1.reg().toX(), 0xf);
      e.LDR(i.dest, X0, X1, IndexExt::LSL, 4);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_VECTOR_SHR, LOAD_VECTOR_SHR_I8);

// ============================================================================
// OPCODE_VECTOR_MAX
// ============================================================================
struct VECTOR_MAX
    : Sequence<VECTOR_MAX, I<OPCODE_VECTOR_MAX, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryVOp(
        e, i, [&i](A64Emitter& e, QReg dest, QReg src1, QReg src2) {
          uint32_t part_type = i.instr->flags >> 8;
          if (i.instr->flags & ARITHMETIC_UNSIGNED) {
            switch (part_type) {
              case INT8_TYPE:
                // e.vpmaxub(dest, src1, src2);
                break;
              case INT16_TYPE:
                // e.vpmaxuw(dest, src1, src2);
                break;
              case INT32_TYPE:
                // e.vpmaxud(dest, src1, src2);
                break;
              default:
                assert_unhandled_case(part_type);
                break;
            }
          } else {
            switch (part_type) {
              case INT8_TYPE:
                // e.vpmaxsb(dest, src1, src2);
                break;
              case INT16_TYPE:
                // e.vpmaxsw(dest, src1, src2);
                break;
              case INT32_TYPE:
                // e.vpmaxsd(dest, src1, src2);
                break;
              default:
                assert_unhandled_case(part_type);
                break;
            }
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_MAX, VECTOR_MAX);

// ============================================================================
// OPCODE_VECTOR_MIN
// ============================================================================
struct VECTOR_MIN
    : Sequence<VECTOR_MIN, I<OPCODE_VECTOR_MIN, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryVOp(
        e, i, [&i](A64Emitter& e, QReg dest, QReg src1, QReg src2) {
          uint32_t part_type = i.instr->flags >> 8;
          if (i.instr->flags & ARITHMETIC_UNSIGNED) {
            switch (part_type) {
              case INT8_TYPE:
                // e.vpminub(dest, src1, src2);
                break;
              case INT16_TYPE:
                // e.vpminuw(dest, src1, src2);
                break;
              case INT32_TYPE:
                // e.vpminud(dest, src1, src2);
                break;
              default:
                assert_unhandled_case(part_type);
                break;
            }
          } else {
            switch (part_type) {
              case INT8_TYPE:
                // e.vpminsb(dest, src1, src2);
                break;
              case INT16_TYPE:
                // e.vpminsw(dest, src1, src2);
                break;
              case INT32_TYPE:
                // e.vpminsd(dest, src1, src2);
                break;
              default:
                assert_unhandled_case(part_type);
                break;
            }
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_MIN, VECTOR_MIN);

// ============================================================================
// OPCODE_VECTOR_COMPARE_EQ
// ============================================================================
struct VECTOR_COMPARE_EQ_V128
    : Sequence<VECTOR_COMPARE_EQ_V128,
               I<OPCODE_VECTOR_COMPARE_EQ, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryVOp(
        e, i, [&i](A64Emitter& e, QReg dest, QReg src1, QReg src2) {
          switch (i.instr->flags) {
            case INT8_TYPE:
              // e.vpcmpeqb(dest, src1, src2);
              break;
            case INT16_TYPE:
              // e.vpcmpeqw(dest, src1, src2);
              break;
            case INT32_TYPE:
              // e.vpcmpeqd(dest, src1, src2);
              break;
            case FLOAT32_TYPE:
              // e.vcmpeqps(dest, src1, src2);
              break;
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_EQ, VECTOR_COMPARE_EQ_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_SGT
// ============================================================================
struct VECTOR_COMPARE_SGT_V128
    : Sequence<VECTOR_COMPARE_SGT_V128,
               I<OPCODE_VECTOR_COMPARE_SGT, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAssociativeBinaryVOp(
        e, i, [&i](A64Emitter& e, QReg dest, QReg src1, QReg src2) {
          switch (i.instr->flags) {
            case INT8_TYPE:
              // e.vpcmpgtb(dest, src1, src2);
              break;
            case INT16_TYPE:
              // e.vpcmpgtw(dest, src1, src2);
              break;
            case INT32_TYPE:
              // e.vpcmpgtd(dest, src1, src2);
              break;
            case FLOAT32_TYPE:
              // e.vcmpgtps(dest, src1, src2);
              break;
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_SGT, VECTOR_COMPARE_SGT_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_SGE
// ============================================================================
struct VECTOR_COMPARE_SGE_V128
    : Sequence<VECTOR_COMPARE_SGE_V128,
               I<OPCODE_VECTOR_COMPARE_SGE, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAssociativeBinaryVOp(
        e, i, [&i](A64Emitter& e, QReg dest, QReg src1, QReg src2) {
          switch (i.instr->flags) {
            case INT8_TYPE:
              // e.vpcmpeqb(Q0, src1, src2);
              // e.vpcmpgtb(dest, src1, src2);
              // e.vpor(dest, Q0);
              break;
            case INT16_TYPE:
              // e.vpcmpeqw(Q0, src1, src2);
              // e.vpcmpgtw(dest, src1, src2);
              // e.vpor(dest, Q0);
              break;
            case INT32_TYPE:
              // e.vpcmpeqd(Q0, src1, src2);
              // e.vpcmpgtd(dest, src1, src2);
              // e.vpor(dest, Q0);
              break;
            case FLOAT32_TYPE:
              // e.vcmpgeps(dest, src1, src2);
              break;
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_SGE, VECTOR_COMPARE_SGE_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_UGT
// ============================================================================
struct VECTOR_COMPARE_UGT_V128
    : Sequence<VECTOR_COMPARE_UGT_V128,
               I<OPCODE_VECTOR_COMPARE_UGT, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {}
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_UGT, VECTOR_COMPARE_UGT_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_UGE
// ============================================================================
struct VECTOR_COMPARE_UGE_V128
    : Sequence<VECTOR_COMPARE_UGE_V128,
               I<OPCODE_VECTOR_COMPARE_UGE, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {}
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_UGE, VECTOR_COMPARE_UGE_V128);

// ============================================================================
// OPCODE_VECTOR_ADD
// ============================================================================
struct VECTOR_ADD
    : Sequence<VECTOR_ADD, I<OPCODE_VECTOR_ADD, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {}
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_ADD, VECTOR_ADD);

// ============================================================================
// OPCODE_VECTOR_SUB
// ============================================================================
struct VECTOR_SUB
    : Sequence<VECTOR_SUB, I<OPCODE_VECTOR_SUB, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {}
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SUB, VECTOR_SUB);

// ============================================================================
// OPCODE_VECTOR_SHL
// ============================================================================

struct VECTOR_SHL_V128
    : Sequence<VECTOR_SHL_V128, I<OPCODE_VECTOR_SHL, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case INT8_TYPE:
        EmitInt8(e, i);
        break;
      case INT16_TYPE:
        EmitInt16(e, i);
        break;
      case INT32_TYPE:
        EmitInt32(e, i);
        break;
      default:
        assert_always();
        break;
    }
  }

  static void EmitInt8(A64Emitter& e, const EmitArgType& i) {}

  static void EmitInt16(A64Emitter& e, const EmitArgType& i) {}

  static void EmitInt32(A64Emitter& e, const EmitArgType& i) {}
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHL, VECTOR_SHL_V128);

// ============================================================================
// OPCODE_VECTOR_SHR
// ============================================================================

struct VECTOR_SHR_V128
    : Sequence<VECTOR_SHR_V128, I<OPCODE_VECTOR_SHR, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case INT8_TYPE:
        EmitInt8(e, i);
        break;
      case INT16_TYPE:
        EmitInt16(e, i);
        break;
      case INT32_TYPE:
        EmitInt32(e, i);
        break;
      default:
        assert_always();
        break;
    }
  }

  static void EmitInt8(A64Emitter& e, const EmitArgType& i) {}

  static void EmitInt16(A64Emitter& e, const EmitArgType& i) {}

  static void EmitInt32(A64Emitter& e, const EmitArgType& i) {}
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHR, VECTOR_SHR_V128);

// ============================================================================
// OPCODE_VECTOR_SHA
// ============================================================================
struct VECTOR_SHA_V128
    : Sequence<VECTOR_SHA_V128, I<OPCODE_VECTOR_SHA, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case INT8_TYPE:
        EmitInt8(e, i);
        break;
      case INT16_TYPE:
        EmitInt16(e, i);
        break;
      case INT32_TYPE:
        EmitInt32(e, i);
        break;
      default:
        assert_always();
        break;
    }
  }

  static void EmitInt8(A64Emitter& e, const EmitArgType& i) {}

  static void EmitInt16(A64Emitter& e, const EmitArgType& i) {}

  static void EmitInt32(A64Emitter& e, const EmitArgType& i) {}
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHA, VECTOR_SHA_V128);

// ============================================================================
// OPCODE_VECTOR_ROTATE_LEFT
// ============================================================================
struct VECTOR_ROTATE_LEFT_V128
    : Sequence<VECTOR_ROTATE_LEFT_V128,
               I<OPCODE_VECTOR_ROTATE_LEFT, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {}
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_ROTATE_LEFT, VECTOR_ROTATE_LEFT_V128);

// ============================================================================
// OPCODE_VECTOR_AVERAGE
// ============================================================================

struct VECTOR_AVERAGE
    : Sequence<VECTOR_AVERAGE,
               I<OPCODE_VECTOR_AVERAGE, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {}
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_AVERAGE, VECTOR_AVERAGE);

// ============================================================================
// OPCODE_INSERT
// ============================================================================
struct INSERT_I8
    : Sequence<INSERT_I8, I<OPCODE_INSERT, V128Op, V128Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    e.MOV(i.dest.reg().Belem()[i.src2.constant() ^ 0x3], i.src3.reg());
  }
};
struct INSERT_I16
    : Sequence<INSERT_I16, I<OPCODE_INSERT, V128Op, V128Op, I8Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    e.MOV(i.dest.reg().Helem()[i.src2.constant() ^ 0x1], i.src3.reg());
  }
};
struct INSERT_I32
    : Sequence<INSERT_I32, I<OPCODE_INSERT, V128Op, V128Op, I8Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    e.MOV(i.dest.reg().Selem()[i.src2.constant()], i.src3.reg());
  }
};
EMITTER_OPCODE_TABLE(OPCODE_INSERT, INSERT_I8, INSERT_I16, INSERT_I32);

// ============================================================================
// OPCODE_EXTRACT
// ============================================================================
// TODO(benvanik): sequence extract/splat:
//  v0.i32 = extract v0.v128, 0
//  v0.v128 = splat v0.i32
// This can be a single broadcast.
struct EXTRACT_I8
    : Sequence<EXTRACT_I8, I<OPCODE_EXTRACT, I8Op, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.UMOV(i.dest, i.src1.reg().Belem()[VEC128_B(i.src2.constant())]);
    } else {
      // Fixup index
      e.EOR(W0, i.src2, 0b11);
      e.AND(W0, W0, 0x1F);
      e.DUP(Q0.B16(), W0);
      // Byte-table lookup
      e.TBL(Q0.B16(), List{i.src1.reg().B16()}, Q0.B16());
      // Get lowest element
      e.UMOV(i.dest, Q0.Belem()[0]);
    }
  }
};
struct EXTRACT_I16
    : Sequence<EXTRACT_I16, I<OPCODE_EXTRACT, I16Op, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.UMOV(i.dest, i.src1.reg().Helem()[VEC128_W(i.src2.constant())]);
    } else {
      // Fixup index
      e.EOR(W0, i.src2, 0b01);
      e.LSL(W0, W0, 1);

      // Replicate index as byte
      e.MOV(W1, 0x01'01);
      e.MUL(W0, W0, W1);

      // Byte indices
      e.ADD(W0, W0, 0x01'00);
      e.UXTH(W0, W0);

      // Replicate byte indices
      e.DUP(Q0.H8(), W0);
      // Byte-table lookup
      e.TBL(Q0.B16(), List{i.src1.reg().B16()}, Q0.B16());
      // Get lowest element
      e.UMOV(i.dest, Q0.Helem()[0]);
    }
  }
};
struct EXTRACT_I32
    : Sequence<EXTRACT_I32, I<OPCODE_EXTRACT, I32Op, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    static const vec128_t extract_table_32[4] = {
        vec128b(3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        vec128b(7, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        vec128b(11, 10, 9, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        vec128b(15, 14, 13, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    };
    if (i.src2.is_constant) {
      e.UMOV(i.dest, i.src1.reg().Selem()[VEC128_D(i.src2.constant())]);
    } else {
      QReg src1 = i.src1.reg();
      if (i.src1.is_constant) {
        src1 = Q1;
        e.LoadConstantV(src1, i.src1.constant());
      }

      e.AND(X0, i.src2.reg().toX(), 0b11);
      e.LSL(X0, X0, 4);

      e.MOVP2R(X1, extract_table_32);
      e.LDR(Q0, X1, X0);

      // Byte-table lookup
      e.TBL(Q0.B16(), List{src1.B16()}, Q0.B16());
      // Get lowest element
      e.UMOV(i.dest, Q0.Selem()[0]);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_EXTRACT, EXTRACT_I8, EXTRACT_I16, EXTRACT_I32);

// ============================================================================
// OPCODE_SPLAT
// ============================================================================
// Copy a value into all elements of a vector
struct SPLAT_I8 : Sequence<SPLAT_I8, I<OPCODE_SPLAT, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.MOV(W0, i.src1.constant());
      e.DUP(Q0.B16(), W0);
    } else {
      e.DUP(Q0.B16(), i.src1);
    }
  }
};
struct SPLAT_I16 : Sequence<SPLAT_I16, I<OPCODE_SPLAT, V128Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.MOV(W0, i.src1.constant());
      e.DUP(Q0.H8(), W0);
    } else {
      e.DUP(Q0.H8(), i.src1);
    }
  }
};
struct SPLAT_I32 : Sequence<SPLAT_I32, I<OPCODE_SPLAT, V128Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.MOV(W0, i.src1.constant());
      e.DUP(Q0.S4(), W0);
    } else {
      e.DUP(Q0.S4(), i.src1);
    }
  }
};
struct SPLAT_F32 : Sequence<SPLAT_F32, I<OPCODE_SPLAT, V128Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.MOV(W0, i.src1.value->constant.i32);
      e.DUP(Q0.S4(), W0);
    } else {
      e.DUP(Q0.S4(), i.src1.reg().toQ().Selem()[0]);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SPLAT, SPLAT_I8, SPLAT_I16, SPLAT_I32, SPLAT_F32);

// ============================================================================
// OPCODE_PERMUTE
// ============================================================================
struct PERMUTE_I32
    : Sequence<PERMUTE_I32, I<OPCODE_PERMUTE, V128Op, I32Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {}
};
struct PERMUTE_V128
    : Sequence<PERMUTE_V128,
               I<OPCODE_PERMUTE, V128Op, V128Op, V128Op, V128Op>> {
  static void EmitByInt8(A64Emitter& e, const EmitArgType& i) {}

  static void EmitByInt16(A64Emitter& e, const EmitArgType& i) {}

  static void EmitByInt32(A64Emitter& e, const EmitArgType& i) {
    assert_always();
  }

  static void Emit(A64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case INT8_TYPE:
        EmitByInt8(e, i);
        break;
      case INT16_TYPE:
        EmitByInt16(e, i);
        break;
      case INT32_TYPE:
        EmitByInt32(e, i);
        break;
      default:
        assert_unhandled_case(i.instr->flags);
        return;
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_PERMUTE, PERMUTE_I32, PERMUTE_V128);

// ============================================================================
// OPCODE_SWIZZLE
// ============================================================================
struct SWIZZLE
    : Sequence<SWIZZLE, I<OPCODE_SWIZZLE, V128Op, V128Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i){};
};
EMITTER_OPCODE_TABLE(OPCODE_SWIZZLE, SWIZZLE);

// ============================================================================
// OPCODE_PACK
// ============================================================================
struct PACK : Sequence<PACK, I<OPCODE_PACK, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags & PACK_TYPE_MODE) {
      case PACK_TYPE_D3DCOLOR:
        EmitD3DCOLOR(e, i);
        break;
      case PACK_TYPE_FLOAT16_2:
        EmitFLOAT16_2(e, i);
        break;
      case PACK_TYPE_FLOAT16_4:
        EmitFLOAT16_4(e, i);
        break;
      case PACK_TYPE_SHORT_2:
        EmitSHORT_2(e, i);
        break;
      case PACK_TYPE_SHORT_4:
        EmitSHORT_4(e, i);
        break;
      case PACK_TYPE_UINT_2101010:
        EmitUINT_2101010(e, i);
        break;
      case PACK_TYPE_ULONG_4202020:
        EmitULONG_4202020(e, i);
        break;
      case PACK_TYPE_8_IN_16:
        Emit8_IN_16(e, i, i.instr->flags);
        break;
      case PACK_TYPE_16_IN_32:
        Emit16_IN_32(e, i, i.instr->flags);
        break;
      default:
        assert_unhandled_case(i.instr->flags);
        break;
    }
  }
  static void EmitD3DCOLOR(A64Emitter& e, const EmitArgType& i) {}
  static void EmitFLOAT16_2(A64Emitter& e, const EmitArgType& i) {}
  static void EmitFLOAT16_4(A64Emitter& e, const EmitArgType& i) {}
  static void EmitSHORT_2(A64Emitter& e, const EmitArgType& i) {}
  static void EmitSHORT_4(A64Emitter& e, const EmitArgType& i) {}
  static void EmitUINT_2101010(A64Emitter& e, const EmitArgType& i) {}
  static void EmitULONG_4202020(A64Emitter& e, const EmitArgType& i) {}
  static void Emit8_IN_16(A64Emitter& e, const EmitArgType& i, uint32_t flags) {
  }
  // Pack 2 32-bit vectors into a 16-bit vector.
  static void Emit16_IN_32(A64Emitter& e, const EmitArgType& i,
                           uint32_t flags) {}
};
EMITTER_OPCODE_TABLE(OPCODE_PACK, PACK);

// ============================================================================
// OPCODE_UNPACK
// ============================================================================
struct UNPACK : Sequence<UNPACK, I<OPCODE_UNPACK, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags & PACK_TYPE_MODE) {
      case PACK_TYPE_D3DCOLOR:
        EmitD3DCOLOR(e, i);
        break;
      case PACK_TYPE_FLOAT16_2:
        EmitFLOAT16_2(e, i);
        break;
      case PACK_TYPE_FLOAT16_4:
        EmitFLOAT16_4(e, i);
        break;
      case PACK_TYPE_SHORT_2:
        EmitSHORT_2(e, i);
        break;
      case PACK_TYPE_SHORT_4:
        EmitSHORT_4(e, i);
        break;
      case PACK_TYPE_UINT_2101010:
        EmitUINT_2101010(e, i);
        break;
      case PACK_TYPE_ULONG_4202020:
        EmitULONG_4202020(e, i);
        break;
      case PACK_TYPE_8_IN_16:
        Emit8_IN_16(e, i, i.instr->flags);
        break;
      case PACK_TYPE_16_IN_32:
        Emit16_IN_32(e, i, i.instr->flags);
        break;
      default:
        assert_unhandled_case(i.instr->flags);
        break;
    }
  }
  static void EmitD3DCOLOR(A64Emitter& e, const EmitArgType& i) {}
  static void EmitFLOAT16_2(A64Emitter& e, const EmitArgType& i) {}
  static void EmitFLOAT16_4(A64Emitter& e, const EmitArgType& i) {}
  static void EmitSHORT_2(A64Emitter& e, const EmitArgType& i) {}
  static void EmitSHORT_4(A64Emitter& e, const EmitArgType& i) {}
  static void EmitUINT_2101010(A64Emitter& e, const EmitArgType& i) {}
  static void EmitULONG_4202020(A64Emitter& e, const EmitArgType& i) {}
  static void Emit8_IN_16(A64Emitter& e, const EmitArgType& i, uint32_t flags) {
  }
  static void Emit16_IN_32(A64Emitter& e, const EmitArgType& i,
                           uint32_t flags) {}
};
EMITTER_OPCODE_TABLE(OPCODE_UNPACK, UNPACK);

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
