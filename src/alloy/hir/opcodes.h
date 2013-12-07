/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_HIR_OPCODES_H_
#define ALLOY_HIR_OPCODES_H_

#include <alloy/core.h>


namespace alloy {
namespace hir {


enum CallFlags {
  CALL_TAIL       = (1 << 1),
};
enum BranchFlags {
  BRANCH_LIKELY   = (1 << 1),
  BRANCH_UNLIKELY = (1 << 2),
};
enum RoundMode {
  // to zero/nearest/etc
  ROUND_TO_ZERO = 0,
  ROUND_TO_NEAREST,
};
enum LoadFlags {
  LOAD_NO_ALIAS   = (1 << 1),
  LOAD_ALIGNED    = (1 << 2),
  LOAD_UNALIGNED  = (1 << 3),
  LOAD_VOLATILE   = (1 << 4),
};
enum StoreFlags {
  STORE_NO_ALIAS  = (1 << 1),
  STORE_ALIGNED   = (1 << 2),
  STORE_UNALIGNED = (1 << 3),
  STORE_VOLATILE  = (1 << 4),
};
enum PrefetchFlags {
  PREFETCH_LOAD   = (1 << 1),
  PREFETCH_STORE  = (1 << 2),
};
enum ArithmeticFlags {
  ARITHMETIC_SET_CARRY = (1 << 1),
};
enum Permutes {
  PERMUTE_XY_ZW = 0x05040100,
};
enum Swizzles {
  SWIZZLE_XYZW_TO_XYZW = 0xE4,
  SWIZZLE_XYZW_TO_YZWX = 0x39,
  SWIZZLE_XYZW_TO_ZWXY = 0x4E,
  SWIZZLE_XYZW_TO_WXYZ = 0x93,
};


enum Opcode {
  OPCODE_COMMENT,

  OPCODE_NOP,

  OPCODE_DEBUG_BREAK,
  OPCODE_DEBUG_BREAK_TRUE,

  OPCODE_TRAP,
  OPCODE_TRAP_TRUE,

  OPCODE_CALL,
  OPCODE_CALL_TRUE,
  OPCODE_CALL_INDIRECT,
  OPCODE_CALL_INDIRECT_TRUE,
  OPCODE_RETURN,

  OPCODE_BRANCH,
  OPCODE_BRANCH_IF,
  OPCODE_BRANCH_TRUE,
  OPCODE_BRANCH_FALSE,

  OPCODE_ASSIGN,
  OPCODE_CAST,
  OPCODE_ZERO_EXTEND,
  OPCODE_SIGN_EXTEND,
  OPCODE_TRUNCATE,
  OPCODE_CONVERT,
  OPCODE_ROUND,
  OPCODE_VECTOR_CONVERT_I2F,
  OPCODE_VECTOR_CONVERT_F2I,

  OPCODE_LOAD_CONTEXT,
  OPCODE_STORE_CONTEXT,

  OPCODE_LOAD,
  OPCODE_LOAD_ACQUIRE,
  OPCODE_STORE,
  OPCODE_STORE_RELEASE,
  OPCODE_PREFETCH,

  OPCODE_MAX,
  OPCODE_MIN,
  OPCODE_SELECT,
  OPCODE_IS_TRUE,
  OPCODE_IS_FALSE,
  OPCODE_COMPARE_EQ,
  OPCODE_COMPARE_NE,
  OPCODE_COMPARE_SLT,
  OPCODE_COMPARE_SLE,
  OPCODE_COMPARE_SGT,
  OPCODE_COMPARE_SGE,
  OPCODE_COMPARE_ULT,
  OPCODE_COMPARE_ULE,
  OPCODE_COMPARE_UGT,
  OPCODE_COMPARE_UGE,
  OPCODE_DID_CARRY,
  OPCODE_DID_OVERFLOW,
  OPCODE_VECTOR_COMPARE_EQ,
  OPCODE_VECTOR_COMPARE_SGT,
  OPCODE_VECTOR_COMPARE_SGE,
  OPCODE_VECTOR_COMPARE_UGT,
  OPCODE_VECTOR_COMPARE_UGE,

  OPCODE_ADD,
  OPCODE_ADD_CARRY,
  OPCODE_SUB,
  OPCODE_MUL,
  OPCODE_DIV,
  OPCODE_REM,
  OPCODE_MULADD,
  OPCODE_MULSUB,
  OPCODE_NEG,
  OPCODE_ABS,
  OPCODE_SQRT,
  OPCODE_RSQRT,
  OPCODE_DOT_PRODUCT_3,
  OPCODE_DOT_PRODUCT_4,

  OPCODE_AND,
  OPCODE_OR,
  OPCODE_XOR,
  OPCODE_NOT,
  OPCODE_SHL,
  OPCODE_VECTOR_SHL,
  OPCODE_SHR,
  OPCODE_SHA,
  OPCODE_ROTATE_LEFT,
  OPCODE_BYTE_SWAP,
  OPCODE_CNTLZ,
  OPCODE_INSERT,
  OPCODE_EXTRACT,
  OPCODE_SPLAT,
  OPCODE_PERMUTE,
  OPCODE_SWIZZLE,

  OPCODE_COMPARE_EXCHANGE,
  OPCODE_ATOMIC_ADD,
  OPCODE_ATOMIC_SUB,
};

enum OpcodeFlags {
  OPCODE_FLAG_BRANCH      = (1 << 1),
  OPCODE_FLAG_MEMORY      = (1 << 2),
  OPCODE_FLAG_COMMUNATIVE = (1 << 3),
  OPCODE_FLAG_VOLATILE    = (1 << 4),
  OPCODE_FLAG_IGNORE      = (1 << 5),
};

enum OpcodeSignatureType {
  // 3 bits max (0-7)
  OPCODE_SIG_TYPE_X = 0,
  OPCODE_SIG_TYPE_L = 1,
  OPCODE_SIG_TYPE_O = 2,
  OPCODE_SIG_TYPE_S = 3,
  OPCODE_SIG_TYPE_V = 4,
};

enum OpcodeSignature {
  OPCODE_SIG_X        = (OPCODE_SIG_TYPE_X),
  OPCODE_SIG_X_L      = (OPCODE_SIG_TYPE_X) | (OPCODE_SIG_TYPE_L << 3),
  OPCODE_SIG_X_O_V    = (OPCODE_SIG_TYPE_X) | (OPCODE_SIG_TYPE_O << 3) | (OPCODE_SIG_TYPE_V << 6),
  OPCODE_SIG_X_S      = (OPCODE_SIG_TYPE_X) | (OPCODE_SIG_TYPE_S << 3),
  OPCODE_SIG_X_V      = (OPCODE_SIG_TYPE_X) | (OPCODE_SIG_TYPE_V << 3),
  OPCODE_SIG_X_V_L    = (OPCODE_SIG_TYPE_X) | (OPCODE_SIG_TYPE_V << 3) | (OPCODE_SIG_TYPE_L << 6),
  OPCODE_SIG_X_V_L_L  = (OPCODE_SIG_TYPE_X) | (OPCODE_SIG_TYPE_V << 3) | (OPCODE_SIG_TYPE_L << 6) | (OPCODE_SIG_TYPE_L << 9),
  OPCODE_SIG_X_V_O    = (OPCODE_SIG_TYPE_X) | (OPCODE_SIG_TYPE_V << 3) | (OPCODE_SIG_TYPE_O << 6),
  OPCODE_SIG_X_V_S    = (OPCODE_SIG_TYPE_X) | (OPCODE_SIG_TYPE_V << 3) | (OPCODE_SIG_TYPE_S << 6),
  OPCODE_SIG_X_V_V    = (OPCODE_SIG_TYPE_X) | (OPCODE_SIG_TYPE_V << 3) | (OPCODE_SIG_TYPE_V << 6),
  OPCODE_SIG_X_V_V_V  = (OPCODE_SIG_TYPE_X) | (OPCODE_SIG_TYPE_V << 3) | (OPCODE_SIG_TYPE_V << 6) | (OPCODE_SIG_TYPE_V << 9),
  OPCODE_SIG_V        = (OPCODE_SIG_TYPE_V),
  OPCODE_SIG_V_O      = (OPCODE_SIG_TYPE_V) | (OPCODE_SIG_TYPE_O << 3),
  OPCODE_SIG_V_V      = (OPCODE_SIG_TYPE_V) | (OPCODE_SIG_TYPE_V << 3),
  OPCODE_SIG_V_V_O    = (OPCODE_SIG_TYPE_V) | (OPCODE_SIG_TYPE_V << 3) | (OPCODE_SIG_TYPE_O << 6),
  OPCODE_SIG_V_V_O_V  = (OPCODE_SIG_TYPE_V) | (OPCODE_SIG_TYPE_V << 3) | (OPCODE_SIG_TYPE_O << 6) | (OPCODE_SIG_TYPE_V << 9),
  OPCODE_SIG_V_V_V    = (OPCODE_SIG_TYPE_V) | (OPCODE_SIG_TYPE_V << 3) | (OPCODE_SIG_TYPE_V << 6),
  OPCODE_SIG_V_V_V_O  = (OPCODE_SIG_TYPE_V) | (OPCODE_SIG_TYPE_V << 3) | (OPCODE_SIG_TYPE_V << 6) | (OPCODE_SIG_TYPE_O << 9),
  OPCODE_SIG_V_V_V_V  = (OPCODE_SIG_TYPE_V) | (OPCODE_SIG_TYPE_V << 3) | (OPCODE_SIG_TYPE_V << 6) | (OPCODE_SIG_TYPE_V << 9),
};

#define GET_OPCODE_SIG_TYPE_DEST(sig) (OpcodeSignatureType)(sig & 0x7)
#define GET_OPCODE_SIG_TYPE_SRC1(sig) (OpcodeSignatureType)((sig >> 3) & 0x7)
#define GET_OPCODE_SIG_TYPE_SRC2(sig) (OpcodeSignatureType)((sig >> 6) & 0x7)
#define GET_OPCODE_SIG_TYPE_SRC3(sig) (OpcodeSignatureType)((sig >> 9) & 0x7)

typedef struct {
  uint32_t    flags;
  uint32_t    signature;
  const char* name;
  Opcode      num;
} OpcodeInfo;


}  // namespace hir
}  // namespace alloy


#endif  // ALLOY_HIR_OPCODES_H_
