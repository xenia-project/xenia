/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/ppc/instr.h>

#include "cpu/ppc/instr_table.h"


xe_ppc_instr_type_t *xe_ppc_get_instr_type(uint32_t code) {
  xe_ppc_instr_type_t *slot = NULL;
  switch (code >> 26) {
  case 4:
    // Opcode = 4, index = bits 5-0 (6)
    slot = &xe_ppc_instr_table_4[XESELECTBITS(code, 0, 5)];
    break;
  case 19:
    // Opcode = 19, index = bits 10-1 (10)
    slot = &xe_ppc_instr_table_19[XESELECTBITS(code, 1, 10)];
    break;
  case 30:
    // Opcode = 30, index = bits 5-1 (5)
    slot = &xe_ppc_instr_table_30[XESELECTBITS(code, 1, 5)];
    break;
  case 31:
    // Opcode = 31, index = bits 10-1 (10)
    slot = &xe_ppc_instr_table_31[XESELECTBITS(code, 1, 10)];
    break;
  case 58:
    // Opcode = 58, index = bits 1-0 (2)
    slot = &xe_ppc_instr_table_58[XESELECTBITS(code, 0, 1)];
    break;
  case 59:
    // Opcode = 59, index = bits 5-1 (5)
    slot = &xe_ppc_instr_table_59[XESELECTBITS(code, 1, 5)];
    break;
  case 62:
    // Opcode = 62, index = bits 1-0 (2)
    slot = &xe_ppc_instr_table_62[XESELECTBITS(code, 0, 1)];
    break;
  case 63:
    // Opcode = 63, index = bits 10-1 (10)
    slot = &xe_ppc_instr_table_63[XESELECTBITS(code, 1, 10)];
    break;
  default:
    slot = &xe_ppc_instr_table[XESELECTBITS(code, 26, 31)];
    break;
  }
  if (!slot || !slot->opcode) {
    return NULL;
  }
  return slot;
}

int xe_ppc_register_instr_emit(uint32_t code, xe_ppc_emit_fn emit) {
  xe_ppc_instr_type_t *instr_type = xe_ppc_get_instr_type(code);
  if (!instr_type) {
    return 1;
  }
  XEASSERTNULL(instr_type->emit);
  instr_type->emit = emit;
  return 0;
}
