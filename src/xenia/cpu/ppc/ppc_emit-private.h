/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_PPC_EMIT_PRIVATE_H_
#define XENIA_CPU_PPC_PPC_EMIT_PRIVATE_H_

#include "xenia/base/logging.h"
#include "xenia/cpu/ppc/ppc_decode_data.h"
#include "xenia/cpu/ppc/ppc_emit.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"

namespace xe {
namespace cpu {
namespace ppc {

#define XEREGISTERINSTR(name) \
  RegisterOpcodeEmitter(PPCOpcode::name, InstrEmit_##name);

#define XEINSTRNOTIMPLEMENTED()                      \
  XELOGE("Unimplemented instruction: {}", __func__); \
  assert_always("Instruction not implemented");

}  // namespace ppc
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PPC_PPC_EMIT_PRIVATE_H_
