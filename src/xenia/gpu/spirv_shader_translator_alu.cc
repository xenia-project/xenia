/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv_shader_translator.h"

namespace xe {
namespace gpu {

void SpirvShaderTranslator::ProcessAluInstruction(
    const ParsedAluInstruction& instr) {
  if (instr.IsNop()) {
    // Don't even disassemble or update predication.
    return;
  }

  UpdateInstructionPredication(instr.is_predicated, instr.predicate_condition);

  // TODO(Triang3l): Translate the ALU instruction.
}

}  // namespace gpu
}  // namespace xe
