/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_LLVMBE_EMIT_H_
#define XENIA_CPU_LLVMBE_EMIT_H_

#include <xenia/cpu/ppc/instr.h>


namespace xe {
namespace cpu {
namespace llvmbe {


void RegisterEmitCategoryALU();
void RegisterEmitCategoryControl();
void RegisterEmitCategoryFPU();
void RegisterEmitCategoryMemory();


#define XEEMITTER(name, opcode, format) int InstrEmit_##name

#define XEREGISTERINSTR(name, opcode) \
    RegisterInstrEmit(opcode, (InstrEmitFn)InstrEmit_##name);

#define XEINSTRNOTIMPLEMENTED()
//#define XEINSTRNOTIMPLEMENTED XEASSERTALWAYS


}  // namespace llvmbe
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_LLVMBE_EMIT_H_
