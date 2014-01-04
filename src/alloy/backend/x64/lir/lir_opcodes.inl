/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

DEFINE_OPCODE(
    LIR_OPCODE_NOP,
    "nop",
    LIR_OPCODE_FLAG_IGNORE)

DEFINE_OPCODE(
    LIR_OPCODE_COMMENT,
    "comment",
    LIR_OPCODE_FLAG_IGNORE)
DEFINE_OPCODE(
    LIR_OPCODE_SOURCE_OFFSET,
    "source_offset",
    LIR_OPCODE_FLAG_IGNORE | LIR_OPCODE_FLAG_HIDE)
DEFINE_OPCODE(
    LIR_OPCODE_DEBUG_BREAK,
    "debug_break",
    0)
DEFINE_OPCODE(
    LIR_OPCODE_TRAP,
    "trap",
    0)

DEFINE_OPCODE(
    LIR_OPCODE_MOV,
    "mov",
    0)
DEFINE_OPCODE(
    LIR_OPCODE_MOV_ZX,
    "mov_zx",
    0)
DEFINE_OPCODE(
    LIR_OPCODE_MOV_SX,
    "mov_sx",
    0)

DEFINE_OPCODE(
    LIR_OPCODE_TEST,
    "test",
    0)

DEFINE_OPCODE(
    LIR_OPCODE_JUMP,
    "jump",
    0)
DEFINE_OPCODE(
    LIR_OPCODE_JUMP_EQ,
    "jump_eq",
    0)
DEFINE_OPCODE(
    LIR_OPCODE_JUMP_NE,
    "jump_ne",
    0)
