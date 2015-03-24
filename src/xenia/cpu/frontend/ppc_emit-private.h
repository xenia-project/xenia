/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_FRONTEND_PPC_EMIT_PRIVATE_H_
#define XENIA_FRONTEND_PPC_EMIT_PRIVATE_H_

#include "xenia/cpu/frontend/ppc_emit.h"
#include "xenia/cpu/frontend/ppc_instr.h"

namespace xe {
namespace cpu {
namespace frontend {

#define XEEMITTER(name, opcode, format) int InstrEmit_##name

#define XEREGISTERINSTR(name, opcode) \
  RegisterInstrEmit(opcode, (InstrEmitFn)InstrEmit_##name);

//#define XEINSTRNOTIMPLEMENTED()
#define XEINSTRNOTIMPLEMENTED() assert_always("Instruction not implemented");
//#define XEINSTRNOTIMPLEMENTED() __debugbreak()

}  // namespace frontend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_FRONTEND_PPC_EMIT_PRIVATE_H_
