/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_FRONTEND_PPC_PPC_EMIT_PRIVATE_H_
#define ALLOY_FRONTEND_PPC_PPC_EMIT_PRIVATE_H_

#include <alloy/frontend/ppc/ppc_emit.h>
#include <alloy/frontend/ppc/ppc_instr.h>


namespace alloy {
namespace frontend {
namespace ppc {


#define XEEMITTER(name, opcode, format) int InstrEmit_##name

#define XEREGISTERINSTR(name, opcode) \
    RegisterInstrEmit(opcode, (InstrEmitFn)InstrEmit_##name);

//#define XEINSTRNOTIMPLEMENTED()
#define XEINSTRNOTIMPLEMENTED XEASSERTALWAYS


}  // namespace ppc
}  // namespace frontend
}  // namespace alloy


#endif  // ALLOY_FRONTEND_PPC_PPC_EMIT_PRIVATE_H_
