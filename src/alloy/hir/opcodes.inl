/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */


DEFINE_OPCODE(
    OPCODE_COMMENT,
    "comment",
    OPCODE_SIG_X_O,
    OPCODE_FLAG_IGNORE)

DEFINE_OPCODE(
    OPCODE_NOP,
    "nop",
    OPCODE_SIG_X,
    OPCODE_FLAG_IGNORE)

DEFINE_OPCODE(
    OPCODE_SOURCE_OFFSET,
    "source_offset",
    OPCODE_SIG_X_O,
    OPCODE_FLAG_IGNORE | OPCODE_FLAG_HIDE)

DEFINE_OPCODE(
    OPCODE_DEBUG_BREAK,
    "debug_break",
    OPCODE_SIG_X,
    OPCODE_FLAG_VOLATILE)

DEFINE_OPCODE(
    OPCODE_DEBUG_BREAK_TRUE,
    "debug_break_true",
    OPCODE_SIG_X_V,
    OPCODE_FLAG_VOLATILE)

DEFINE_OPCODE(
    OPCODE_TRAP,
    "trap",
    OPCODE_SIG_X,
    OPCODE_FLAG_VOLATILE)

DEFINE_OPCODE(
    OPCODE_TRAP_TRUE,
    "trap_true",
    OPCODE_SIG_X_V,
    OPCODE_FLAG_VOLATILE)

DEFINE_OPCODE(
    OPCODE_CALL,
    "call",
    OPCODE_SIG_X_S,
    OPCODE_FLAG_BRANCH)

DEFINE_OPCODE(
    OPCODE_CALL_TRUE,
    "call_true",
    OPCODE_SIG_X_V_S,
    OPCODE_FLAG_BRANCH)

DEFINE_OPCODE(
    OPCODE_CALL_INDIRECT,
    "call_indirect",
    OPCODE_SIG_X_V,
    OPCODE_FLAG_BRANCH)

DEFINE_OPCODE(
    OPCODE_CALL_INDIRECT_TRUE,
    "call_indirect_true",
    OPCODE_SIG_X_V_V,
    OPCODE_FLAG_BRANCH)

DEFINE_OPCODE(
    OPCODE_CALL_EXTERN,
    "call_extern",
    OPCODE_SIG_X_S,
    OPCODE_FLAG_BRANCH)

DEFINE_OPCODE(
    OPCODE_RETURN,
    "return",
    OPCODE_SIG_X,
    OPCODE_FLAG_BRANCH)

DEFINE_OPCODE(
    OPCODE_RETURN_TRUE,
    "return_true",
    OPCODE_SIG_X_V,
    OPCODE_FLAG_BRANCH)

DEFINE_OPCODE(
    OPCODE_SET_RETURN_ADDRESS,
    "set_return_address",
    OPCODE_SIG_X_V,
    0)

DEFINE_OPCODE(
    OPCODE_BRANCH,
    "branch",
    OPCODE_SIG_X_L,
    OPCODE_FLAG_BRANCH)

DEFINE_OPCODE(
    OPCODE_BRANCH_TRUE,
    "branch_true",
    OPCODE_SIG_X_V_L,
    OPCODE_FLAG_BRANCH)

DEFINE_OPCODE(
    OPCODE_BRANCH_FALSE,
    "branch_false",
    OPCODE_SIG_X_V_L,
    OPCODE_FLAG_BRANCH)

DEFINE_OPCODE(
    OPCODE_ASSIGN,
    "assign",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_CAST,
    "cast",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_ZERO_EXTEND,
    "zero_extend",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_SIGN_EXTEND,
    "sign_extend",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_TRUNCATE,
    "truncate",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_CONVERT,
    "convert",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_ROUND,
    "round",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_VECTOR_CONVERT_I2F,
    "vector_convert_i2f",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_VECTOR_CONVERT_F2I,
    "vector_convert_f2i",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_LOAD_VECTOR_SHL,
    "load_vector_shl",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_LOAD_VECTOR_SHR,
    "load_vector_shr",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_LOAD_CLOCK,
    "load_clock",
    OPCODE_SIG_V,
    0)

DEFINE_OPCODE(
    OPCODE_LOAD_LOCAL,
    "load_local",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_STORE_LOCAL,
    "store_local",
    OPCODE_SIG_X_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_LOAD_CONTEXT,
    "load_context",
    OPCODE_SIG_V_O,
    0)

DEFINE_OPCODE(
    OPCODE_STORE_CONTEXT,
    "store_context",
    OPCODE_SIG_X_O_V,
    0)

DEFINE_OPCODE(
    OPCODE_LOAD,
    "load",
    OPCODE_SIG_V_V,
    OPCODE_FLAG_MEMORY)

DEFINE_OPCODE(
    OPCODE_STORE,
    "store",
    OPCODE_SIG_X_V_V,
    OPCODE_FLAG_MEMORY)

DEFINE_OPCODE(
    OPCODE_PREFETCH,
    "prefetch",
    OPCODE_SIG_X_V_O,
    0)

DEFINE_OPCODE(
    OPCODE_MAX,
    "max",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_MIN,
    "min",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_SELECT,
    "select",
    OPCODE_SIG_V_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_IS_TRUE,
    "is_true",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_IS_FALSE,
    "is_false",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_COMPARE_EQ,
    "compare_eq",
    OPCODE_SIG_V_V_V,
    OPCODE_FLAG_COMMUNATIVE)
DEFINE_OPCODE(
    OPCODE_COMPARE_NE,
    "compare_ne",
    OPCODE_SIG_V_V_V,
    OPCODE_FLAG_COMMUNATIVE)
DEFINE_OPCODE(
    OPCODE_COMPARE_SLT,
    "compare_slt",
    OPCODE_SIG_V_V_V,
    0)
DEFINE_OPCODE(
    OPCODE_COMPARE_SLE,
    "compare_sle",
    OPCODE_SIG_V_V_V,
    0)
DEFINE_OPCODE(
    OPCODE_COMPARE_SGT,
    "compare_sgt",
    OPCODE_SIG_V_V_V,
    0)
DEFINE_OPCODE(
    OPCODE_COMPARE_SGE,
    "compare_sge",
    OPCODE_SIG_V_V_V,
    0)
DEFINE_OPCODE(
    OPCODE_COMPARE_ULT,
    "compare_ult",
    OPCODE_SIG_V_V_V,
    0)
DEFINE_OPCODE(
    OPCODE_COMPARE_ULE,
    "compare_ule",
    OPCODE_SIG_V_V_V,
    0)
DEFINE_OPCODE(
    OPCODE_COMPARE_UGT,
    "compare_ugt",
    OPCODE_SIG_V_V_V,
    0)
DEFINE_OPCODE(
    OPCODE_COMPARE_UGE,
    "compare_uge",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_DID_CARRY,
    "did_carry",
    OPCODE_SIG_V_V,
    OPCODE_FLAG_PAIRED_PREV)
DEFINE_OPCODE(
    OPCODE_DID_OVERFLOW,
    "did_overflow",
    OPCODE_SIG_V_V,
    OPCODE_FLAG_PAIRED_PREV)
DEFINE_OPCODE(
    OPCODE_DID_SATURATE,
    "did_saturate",
    OPCODE_SIG_V_V,
    OPCODE_FLAG_PAIRED_PREV)

DEFINE_OPCODE(
    OPCODE_VECTOR_COMPARE_EQ,
    "vector_compare_eq",
    OPCODE_SIG_V_V_V,
    OPCODE_FLAG_COMMUNATIVE)
DEFINE_OPCODE(
    OPCODE_VECTOR_COMPARE_SGT,
    "vector_compare_sgt",
    OPCODE_SIG_V_V_V,
    0)
DEFINE_OPCODE(
    OPCODE_VECTOR_COMPARE_SGE,
    "vector_compare_sge",
    OPCODE_SIG_V_V_V,
    0)
DEFINE_OPCODE(
    OPCODE_VECTOR_COMPARE_UGT,
    "vector_compare_ugt",
    OPCODE_SIG_V_V_V,
    0)
DEFINE_OPCODE(
    OPCODE_VECTOR_COMPARE_UGE,
    "vector_compare_uge",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_ADD,
    "add",
    OPCODE_SIG_V_V_V,
    OPCODE_FLAG_COMMUNATIVE)

DEFINE_OPCODE(
    OPCODE_ADD_CARRY,
    "add_carry",
    OPCODE_SIG_V_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_VECTOR_ADD,
    "vector_add",
    OPCODE_SIG_V_V_V,
    OPCODE_FLAG_COMMUNATIVE)

DEFINE_OPCODE(
    OPCODE_SUB,
    "sub",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_MUL,
    "mul",
    OPCODE_SIG_V_V_V,
    OPCODE_FLAG_COMMUNATIVE)

DEFINE_OPCODE(
    OPCODE_MUL_HI,
    "mul_hi",
    OPCODE_SIG_V_V_V,
    OPCODE_FLAG_COMMUNATIVE)

DEFINE_OPCODE(
    OPCODE_DIV,
    "div",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_MUL_ADD,
    "mul_add",
    OPCODE_SIG_V_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_MUL_SUB,
    "mul_sub",
    OPCODE_SIG_V_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_NEG,
    "neg",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_ABS,
    "abs",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_SQRT,
    "sqrt",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_RSQRT,
    "rsqrt",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_POW2,
    "pow2",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_LOG2,
    "log2",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_DOT_PRODUCT_3,
    "dot_product_3",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_DOT_PRODUCT_4,
    "dot_product_4",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_AND,
    "and",
    OPCODE_SIG_V_V_V,
    OPCODE_FLAG_COMMUNATIVE)

DEFINE_OPCODE(
    OPCODE_OR,
    "or",
    OPCODE_SIG_V_V_V,
    OPCODE_FLAG_COMMUNATIVE)

DEFINE_OPCODE(
    OPCODE_XOR,
    "xor",
    OPCODE_SIG_V_V_V,
    OPCODE_FLAG_COMMUNATIVE)

DEFINE_OPCODE(
    OPCODE_NOT,
    "not",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_SHL,
    "shl",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_VECTOR_SHL,
    "vector_shl",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_SHR,
    "shr",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_VECTOR_SHR,
    "vector_shr",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_SHA,
    "sha",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_VECTOR_SHA,
    "vector_sha",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_ROTATE_LEFT,
    "rotate_left",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_BYTE_SWAP,
    "byte_swap",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_CNTLZ,
    "cntlz",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_INSERT,
    "insert",
    OPCODE_SIG_V_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_EXTRACT,
    "extract",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_SPLAT,
    "splat",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_PERMUTE,
    "permute",
    OPCODE_SIG_V_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_SWIZZLE,
    "swizzle",
    OPCODE_SIG_V_V_O,
    0)

DEFINE_OPCODE(
    OPCODE_PACK,
    "pack",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_UNPACK,
    "unpack",
    OPCODE_SIG_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_COMPARE_EXCHANGE,
    "compare_exchange",
    OPCODE_SIG_V_V_V_V,
    OPCODE_FLAG_VOLATILE)

DEFINE_OPCODE(
    OPCODE_ATOMIC_EXCHANGE,
    "atomic_exchange",
    OPCODE_SIG_V_V_V,
    OPCODE_FLAG_VOLATILE)

DEFINE_OPCODE(
    OPCODE_ATOMIC_ADD,
    "atomic_add",
    OPCODE_SIG_V_V_V,
    0)

DEFINE_OPCODE(
    OPCODE_ATOMIC_SUB,
    "atomic_sub",
    OPCODE_SIG_V_V_V,
    0)
