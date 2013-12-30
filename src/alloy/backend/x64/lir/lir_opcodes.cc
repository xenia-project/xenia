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

#define DEFINE_OPCODE(num, name, sig, flags) \
    static const LIROpcodeInfo num##_info = { flags, sig, name, num, };
#include <alloy/backend/x64/lir/lir_opcodes.inl>
#undef DEFINE_OPCODE

}  // namespace lir
}  // namespace x64
}  // namespace backend
}  // namespace alloy
