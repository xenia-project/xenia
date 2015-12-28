/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_PPC_DISASM_H_
#define XENIA_CPU_PPC_PPC_DISASM_H_

#include "xenia/base/string_buffer.h"

namespace xe {
namespace cpu {
namespace ppc {

int DisasmPPC(uint32_t address, uint32_t code, StringBuffer* str);

}  // namespace ppc
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PPC_PPC_DISASM_H_
