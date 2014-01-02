/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_LIR_LIR_OPCODES_H_
#define ALLOY_BACKEND_X64_LIR_LIR_OPCODES_H_

#include <alloy/core.h>


namespace alloy {
namespace backend {
namespace x64 {
namespace lir {


enum LIROpcode {
  LIR_OPCODE_COMMENT,
  LIR_OPCODE_NOP,

  LIR_OPCODE_SOURCE_OFFSET,

  LIR_OPCODE_DEBUG_BREAK,
  LIR_OPCODE_TRAP,

  LIR_OPCODE_MOV,

  LIR_OPCODE_TEST,

  LIR_OPCODE_JUMP_EQ,
  LIR_OPCODE_JUMP_NE,

  __LIR_OPCODE_MAX_VALUE, // Keep at end.
};

enum LIROpcodeFlags {
  LIR_OPCODE_FLAG_BRANCH      = (1 << 1),
  LIR_OPCODE_FLAG_MEMORY      = (1 << 2),
  LIR_OPCODE_FLAG_COMMUNATIVE = (1 << 3),
  LIR_OPCODE_FLAG_VOLATILE    = (1 << 4),
  LIR_OPCODE_FLAG_IGNORE      = (1 << 5),
  LIR_OPCODE_FLAG_HIDE        = (1 << 6),
};

typedef struct {
  uint32_t    flags;
  const char* name;
  LIROpcode   num;
} LIROpcodeInfo;


#define DEFINE_OPCODE(num, string_name, flags) \
    extern const LIROpcodeInfo num##_info;
#include <alloy/backend/x64/lir/lir_opcodes.inl>
#undef DEFINE_OPCODE

extern const LIROpcodeInfo& GetOpcodeInfo(LIROpcode opcode);


}  // namespace lir
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_LIR_LIR_OPCODES_H_
