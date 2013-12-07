/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_FRONTEND_PPC_PPC_DISASM_PRIVATE_H_
#define ALLOY_FRONTEND_PPC_PPC_DISASM_PRIVATE_H_

#include <alloy/frontend/ppc/ppc_disasm.h>
#include <alloy/frontend/ppc/ppc_instr.h>


namespace alloy {
namespace frontend {
namespace ppc {


#define XEDISASMR(name, opcode, format) int InstrDisasm_##name

#define XEREGISTERINSTR(name, opcode) \
    RegisterInstrDisassemble(opcode, (InstrDisassembleFn)InstrDisasm_##name);

//#define XEINSTRNOTIMPLEMENTED()
#define XEINSTRNOTIMPLEMENTED XEASSERTALWAYS


}  // namespace ppc
}  // namespace frontend
}  // namespace alloy


#endif  // ALLOY_FRONTEND_PPC_PPC_DISASM_PRIVATE_H_
