/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_FRONTEND_PPC_DISASM_H_
#define XENIA_CPU_FRONTEND_PPC_DISASM_H_

#include "xenia/base/string_buffer.h"
#include "xenia/cpu/frontend/ppc_instr.h"

namespace xe {
namespace cpu {
namespace frontend {

int DisasmPPC(InstrData* i, StringBuffer* str);

}  // namespace frontend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_FRONTEND_PPC_DISASM_H_
