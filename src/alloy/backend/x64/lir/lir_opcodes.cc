/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/lir/lir_opcodes.h>

using namespace alloy;
using namespace alloy::backend::x64::lir;


namespace alloy {
namespace backend {
namespace x64 {
namespace lir {

#define DEFINE_OPCODE(num, string_name, flags) \
    static const LIROpcodeInfo num##_info = { flags, string_name, num, };
#include <alloy/backend/x64/lir/lir_opcodes.inl>
#undef DEFINE_OPCODE

const LIROpcodeInfo& GetOpcodeInfo(LIROpcode opcode) {
  static const LIROpcodeInfo* lookup[__LIR_OPCODE_MAX_VALUE] = {
#define DEFINE_OPCODE(num, string_name, flags) \
    &num##_info,
#include <alloy/backend/x64/lir/lir_opcodes.inl>
#undef DEFINE_OPCODE
  };
  return *lookup[opcode];
}

}  // namespace lir
}  // namespace x64
}  // namespace backend
}  // namespace alloy
