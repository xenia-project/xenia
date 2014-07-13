/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/hir/opcodes.h>

namespace alloy {
namespace hir {

#define DEFINE_OPCODE(num, name, sig, flags) \
  const OpcodeInfo num##_info = {            \
      flags, sig, name, num,                 \
  };
#include <alloy/hir/opcodes.inl>
#undef DEFINE_OPCODE

}  // namespace hir
}  // namespace alloy
