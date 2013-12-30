/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

DEFINE_OPCODE(
    LIR_OPCODE_MOV_I32,
    "mov.i32",
    LIR_OPCODE_SIG_R32_R32,
    0);
DEFINE_OPCODE(
    LIR_OPCODE_XOR_I32,
    "xor.i32",
    LIR_OPCODE_SIG_R32_R32,
    0);
DEFINE_OPCODE(
    LIR_OPCODE_DEC_I32,
    "dec.i32",
    LIR_OPCODE_SIG_R32,
    0);
DEFINE_OPCODE(
    LIR_OPCODE_SUB_I32,
    "sub.i32",
    LIR_OPCODE_SIG_R32_R32,
    0);
DEFINE_OPCODE(
    LIR_OPCODE_IMUL_I32,
    "imul.i32",
    LIR_OPCODE_SIG_R32_R32,
    0);
DEFINE_OPCODE(
    LIR_OPCODE_IMUL_I32_AUX,
    "imul.i32.aux",
    LIR_OPCODE_SIG_R32_R32_C32,
    0);
DEFINE_OPCODE(
    LIR_OPCODE_DIV_I32,
    "div.i32",
    LIR_OPCODE_SIG_R32,
    0);
