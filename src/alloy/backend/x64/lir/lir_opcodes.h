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
  LIR_OPCODE_MOV_I32,
  LIR_OPCODE_XOR_I32,
  LIR_OPCODE_DEC_I32,
  LIR_OPCODE_SUB_I32,
  LIR_OPCODE_IMUL_I32,
  LIR_OPCODE_IMUL_I32_AUX,
  LIR_OPCODE_DIV_I32,

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

enum LIROpcodeSignatureType {
  // 3 bits max (0-7)
  LIR_OPCODE_SIG_TYPE_X = 0,
};

enum LIROpcodeSignature {
  LIR_OPCODE_SIG_X = (LIR_OPCODE_SIG_TYPE_X),
  LIR_OPCODE_SIG_R32 = (LIR_OPCODE_SIG_TYPE_X),
  LIR_OPCODE_SIG_R32_R32 = (LIR_OPCODE_SIG_TYPE_X),
  LIR_OPCODE_SIG_R32_R32_C32 = (LIR_OPCODE_SIG_TYPE_X),
};

#define GET_LIR_OPCODE_SIG_TYPE_DEST(sig) (LIROpcodeSignatureType)(sig & 0x7)
#define GET_LIR_OPCODE_SIG_TYPE_SRC1(sig) (LIROpcodeSignatureType)((sig >> 3) & 0x7)
#define GET_LIR_OPCODE_SIG_TYPE_SRC2(sig) (LIROpcodeSignatureType)((sig >> 6) & 0x7)
#define GET_LIR_OPCODE_SIG_TYPE_SRC3(sig) (LIROpcodeSignatureType)((sig >> 9) & 0x7)

typedef struct {
  uint32_t    flags;
  uint32_t    signature;
  const char* name;
  LIROpcode   num;
} LIROpcodeInfo;


#define DEFINE_OPCODE(num, name, sig, flags) \
    extern const LIROpcodeInfo num##_info;
#include <alloy/backend/x64/lir/lir_opcodes.inl>
#undef DEFINE_OPCODE


}  // namespace lir
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_LIR_LIR_OPCODES_H_
