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
#include "xenia/cpu/ppc/ppc_emit.h"
#include "xenia/cpu/ppc/ppc_instr.h"

namespace xe {
namespace cpu {
namespace ppc {

#define XEEMITTER(name, opcode, format) int InstrEmit_##name

#define XEREGISTERINSTR(name, opcode) \
  RegisterInstrEmit(opcode, (InstrEmitFn)InstrEmit_##name);

#define XEINSTRNOTIMPLEMENTED()                          \
  XELOGE("Unimplemented instruction: %s", __FUNCTION__); \
  assert_always("Instruction not implemented");

}  // namespace ppc
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PPC_PPC_EMIT_PRIVATE_H_
