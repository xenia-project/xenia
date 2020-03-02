/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/ppc/ppc_opcode_info.h"

#include "xenia/cpu/ppc/ppc_decode_data.h"

namespace xe {
namespace cpu {
namespace ppc {

bool DisasmPPC(uint32_t address, uint32_t code, StringBuffer* str) {
  auto opcode = LookupOpcode(code);
  if (opcode == PPCOpcode::kInvalid) {
    str->Append("DISASM ERROR");
    return false;
  }
  auto& disasm_info = GetOpcodeDisasmInfo(opcode);
  if (disasm_info.disasm) {
    PPCDecodeData d;
    d.address = address;
    d.code = code;
    disasm_info.disasm(d, str);
  } else {
    str->AppendFormat("{:<8}", disasm_info.name);
  }
  return true;
}

}  // namespace ppc
}  // namespace cpu
}  // namespace xe
