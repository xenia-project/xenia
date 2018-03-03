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
      flags,                                 \
      sig,                                   \
      name,                                  \
      num,                                   \
  };
#include "xenia/cpu/hir/opcodes.inl"
#undef DEFINE_OPCODE

}  // namespace hir
}  // namespace cpu
}  // namespace xe
