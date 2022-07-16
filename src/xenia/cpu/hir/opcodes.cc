/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/hir/opcodes.h"

namespace xe {
namespace cpu {
namespace hir {

#define DEFINE_OPCODE(num, name, sig, flags) \
  const OpcodeInfo num##_info = {            \
      num,                                   \
      flags,                                 \
      sig,                                   \
  };
#include "xenia/cpu/hir/opcodes.inl"
#undef DEFINE_OPCODE

const char* GetOpcodeName(Opcode num) {
  switch (num) {
#define DEFINE_OPCODE(num, name, sig, flags) \
  case num:                                  \
    return name;
#include "xenia/cpu/hir/opcodes.inl"
#undef DEFINE_OPCODE
  }
  return "invalid opcode";
}
}  // namespace hir
}  // namespace cpu
}  // namespace xe
